// SPDX-License-Identifier: GPL-2.0-only
#include "owon_psu.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome::owon_psu {

static const char *const TAG = "owon_psu";

// Consecutive unanswered queries before the PSU counts as gone.
static constexpr uint8_t MAX_MISSED_REPLIES = 3;
// Longer than the longest a reply has been seen to take (about 1 s, after
// OUTP ON/OFF). After a miss the line must be silent this long before the
// resync *IDN? goes out, so no earlier reply is still on its way; the *IDN?
// is then the only query outstanding and is given as long to answer.
static constexpr uint32_t RESYNC_QUIET_MS = 1500;
// The PSU resolves 10 mV and 10 mA, and echoes three decimals.
static constexpr float SETTING_TOLERANCE = 0.006f;

static const char *const SETTING_SCPI[] = {"VOLT", "CURR", "VOLT:LIM", "CURR:LIM", "OUTP"};
static const char *const SETTING_NAME[] = {"voltage", "current", "voltage limit", "current limit", "output"};
static const char *const COMMAND_TEXT[] = {
    nullptr, "*IDN?", "MEAS:ALL:INFO?", "VOLT?", "CURR?", "VOLT:LIM?", "CURR:LIM?", "OUTP?",
};

// Only the *IDN? reply has four fields and a first one that is not a number,
// so no other reply, however late, can be taken for it.
static bool is_identity(const char *reply) {
  int commas = 0;
  for (const char *p = reply; *p != '\0'; p++)
    commas += *p == ',';
  char *end;
  strtof(reply, &end);
  return commas == 3 && end == reply;
}

static Command read_command(Setting s) {
  return static_cast<Command>(static_cast<uint8_t>(CMD_READ_VOLTAGE) + static_cast<uint8_t>(s));
}

void OwonPSUNumber::control(float value) { this->parent_->request_setting(this->setting_, value); }

void OwonPSUSwitch::write_state(bool state) { this->parent_->request_setting(SETTING_OUTPUT, state ? 1.0f : 0.0f); }

void OwonPSU::setup() {
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    this->actual_[i] = NAN;
    this->target_[i] = NAN;
  }
  this->publish_flag_(FLAG_CONNECTED, false);
}

void OwonPSU::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Owon PSU:\n"
                "  Status interval: %ums\n"
                "  Settings interval: %ums\n"
                "  Reply timeout: %ums\n"
                "  Output off after API loss: %ums",
                this->status_interval_, this->settings_interval_, this->reply_timeout_, this->link_loss_timeout_);
}

void OwonPSU::request_setting(Setting s, float value) {
  number::Number *cap = s == SETTING_VOLTAGE ? this->voltage_cap_ : s == SETTING_CURRENT ? this->current_cap_ : nullptr;
  const bool capped = cap != nullptr && cap->has_state() && value > cap->state;
  if (capped || !this->connected_) {
    if (capped) {
      ESP_LOGW(TAG, "%s %.3f refused: above the %.3f cap", SETTING_NAME[s], value, cap->state);
    } else {
      ESP_LOGW(TAG, "%s %.3f refused: PSU not connected", SETTING_NAME[s], value);
    }
    // The entity already shows the request; put it back to what the PSU holds.
    if (s == SETTING_OUTPUT && this->output_switch_ != nullptr && !std::isnan(this->actual_[s])) {
      this->output_switch_->publish_state(this->actual_[s] > 0.5f);
    } else if (this->numbers_[s] != nullptr && !std::isnan(this->actual_[s])) {
      this->numbers_[s]->publish_state(this->actual_[s]);
    }
    return;
  }
  ESP_LOGI(TAG, "%s -> %.3f requested", SETTING_NAME[s], value);
  this->target_[s] = value;
  this->sent_[s] = false;
}

void OwonPSU::suspend_usb() {
  this->suspended_ = true;
  this->in_flight_ = CMD_NONE;
  this->usb_channel_->get_parent()->suspend_usb();
}

void OwonPSU::resume_usb() {
  this->usb_channel_->get_parent()->resume_usb();
  // Whatever arrived around the halt answers nothing still in flight.
  uint8_t c;
  while (this->available() && this->read_byte(&c)) {
  }
  this->rx_len_ = 0;
  this->resync_ = true;
  this->suspended_ = false;
}

void OwonPSU::loop() {
  this->check_link_();
  if (this->suspended_)
    return;
  // No bridge enumerated: there is nothing to ask, and a query would only be
  // dropped by usb_uart and then counted as a PSU that stopped answering.
  if (!this->usb_channel_->is_connected()) {
    this->in_flight_ = CMD_NONE;
    this->rx_len_ = 0;
    this->resync_ = true;
    this->set_connected_(false);
    return;
  }

  uint8_t c;
  while (this->available() && this->read_byte(&c)) {
    this->last_rx_ = millis();
    if (c == '\r')
      continue;
    if (c != '\n') {
      // An overlong line is cut, not wrapped into the next reply.
      if (this->rx_len_ < MAX_REPLY - 1)
        this->rx_buf_[this->rx_len_++] = static_cast<char>(c);
      continue;
    }
    this->rx_buf_[this->rx_len_] = '\0';
    this->rx_len_ = 0;
    if (this->resync_) {
      if (this->in_flight_ != CMD_IDENTITY || !is_identity(this->rx_buf_)) {
        ESP_LOGD(TAG, "resyncing, dropped '%s'", this->rx_buf_);
        continue;
      }
      this->resync_ = false;
    }
    const Command answered = this->in_flight_;
    this->in_flight_ = CMD_NONE;
    if (answered == CMD_NONE) {
      ESP_LOGW(TAG, "unsolicited reply '%s'", this->rx_buf_);
      continue;
    }
    ESP_LOGV(TAG, "%s -> '%s'", COMMAND_TEXT[answered], this->rx_buf_);
    this->on_reply_(answered, this->rx_buf_);
  }

  const uint32_t now = millis();
  if (this->in_flight_ != CMD_NONE) {
    const uint32_t timeout = this->resync_ ? RESYNC_QUIET_MS : this->reply_timeout_;
    if (now - this->command_started_ < timeout)
      return;
    const Command missed = this->in_flight_;
    this->in_flight_ = CMD_NONE;
    this->rx_len_ = 0;
    this->on_timeout_(missed);
  }

  // Replies carry nothing that names their query, so one that arrives after
  // its timeout would answer the next query instead, and every one after it.
  // Until the *IDN? reply comes back, nothing else is asked and nothing else
  // is believed; the PSU answers in order, so pairing holds from there.
  // The *IDN? waits for a quiet line: sent while a late reply is still on its
  // way, it would be one of two outstanding, and the second would misalign.
  if (this->resync_) {
    if (now - this->last_rx_ >= RESYNC_QUIET_MS)
      this->send_query_(CMD_IDENTITY);
    return;
  }

  if (this->connected_) {
    // Switching off goes first; switching on goes last, so the output never
    // comes up on a setpoint that is about to change. A limit is raised before
    // its setpoint and lowered after it, since the PSU refuses a setpoint above
    // its limit and a limit below its setpoint.
    auto pending = [this](Setting s) {
      return !std::isnan(this->target_[s]) && !this->sent_[s];
    };
    auto raising = [this](Setting s) { return std::isnan(this->actual_[s]) || this->target_[s] > this->actual_[s]; };
    static const Setting ORDER[] = {SETTING_VOLTAGE_LIMIT, SETTING_CURRENT_LIMIT, SETTING_VOLTAGE, SETTING_CURRENT};
    if (pending(SETTING_OUTPUT) && this->target_[SETTING_OUTPUT] < 0.5f) {
      this->send_setting_(SETTING_OUTPUT);
      return;
    }
    for (Setting s : ORDER) {
      const bool limit = s == SETTING_VOLTAGE_LIMIT || s == SETTING_CURRENT_LIMIT;
      if (pending(s) && (!limit || raising(s))) {
        this->send_setting_(s);
        return;
      }
    }
    for (Setting s : {SETTING_VOLTAGE_LIMIT, SETTING_CURRENT_LIMIT, SETTING_OUTPUT}) {
      if (pending(s)) {
        this->send_setting_(s);
        return;
      }
    }

    // A set whose read-back went unanswered is asked about again, never resent:
    // after OUTP ON the PSU ignores queries for up to a second, and a second
    // OUTP ON would only restart that.
    for (uint8_t i = 0; i < SETTING_COUNT; i++) {
      if (!std::isnan(this->target_[i]) && this->sent_[i]) {
        this->send_query_(read_command(static_cast<Setting>(i)));
        return;
      }
    }

  }

  if (now - this->last_status_ >= this->status_interval_) {
    this->last_status_ = now;
    this->send_query_(CMD_INFO);
    return;
  }
  if (!this->connected_)
    return;
  if (now - this->last_settings_ >= this->settings_interval_) {
    this->last_settings_ = now;
    this->next_read_ = 0;
  }
  if (this->next_read_ < SETTING_COUNT)
    this->send_query_(read_command(static_cast<Setting>(this->next_read_++)));
}

void OwonPSU::send_query_(Command cmd) {
  char buf[24];
  const int len = snprintf(buf, sizeof(buf), "%s\n", COMMAND_TEXT[cmd]);
  this->write_array(reinterpret_cast<const uint8_t *>(buf), len);
  this->in_flight_ = cmd;
  this->command_started_ = millis();
}

void OwonPSU::send_setting_(Setting s) {
  char buf[32];
  int len;
  if (s == SETTING_OUTPUT) {
    len = snprintf(buf, sizeof(buf), "%s %s\n", SETTING_SCPI[s], this->target_[s] > 0.5f ? "ON" : "OFF");
  } else {
    len = snprintf(buf, sizeof(buf), "%s %.3f\n", SETTING_SCPI[s], this->target_[s]);
  }
  ESP_LOGV(TAG, "-> '%.*s'", len - 1, buf);
  this->write_array(reinterpret_cast<const uint8_t *>(buf), len);
  this->sent_[s] = true;
  // The set itself is never answered. The read-back that follows is, and the
  // PSU handles commands in order, so it reports the outcome of the set.
  this->send_query_(read_command(s));
}

void OwonPSU::on_reply_(Command answered, const char *reply) {
  this->missed_replies_ = 0;
  this->set_connected_(true);
  switch (answered) {
    case CMD_IDENTITY:
      this->parse_identity_(reply);
      break;
    case CMD_INFO:
      this->parse_info_(reply);
      break;
    case CMD_READ_OUTPUT:
      if (strcmp(reply, "ON") == 0 || strcmp(reply, "OFF") == 0) {
        this->on_read_back_(SETTING_OUTPUT, reply[1] == 'N' ? 1.0f : 0.0f);
      } else {
        ESP_LOGW(TAG, "OUTP? -> '%s'", reply);
      }
      break;
    default: {
      char *end;
      const float value = strtof(reply, &end);
      if (end == reply || *end != '\0') {
        ESP_LOGW(TAG, "%s -> '%s'", COMMAND_TEXT[answered], reply);
        break;
      }
      this->on_read_back_(static_cast<Setting>(answered - CMD_READ_VOLTAGE), value);
      break;
    }
  }
}

void OwonPSU::on_timeout_(Command missed) {
  ESP_LOGD(TAG, "%s unanswered", COMMAND_TEXT[missed]);
  this->resync_ = true;
  // The quiet window before the next *IDN? starts now, not at the last byte.
  this->last_rx_ = millis();
  if (++this->missed_replies_ >= MAX_MISSED_REPLIES)
    this->set_connected_(false);
}

void OwonPSU::parse_identity_(const char *reply) {
  // OWON,SPE6103,24121050,FV:V5.1.0
  char buf[MAX_REPLY];
  strncpy(buf, reply, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char *fields[4]{};
  char *save = nullptr;
  size_t n = 0;
  for (char *tok = strtok_r(buf, ",", &save); tok != nullptr && n < 4; tok = strtok_r(nullptr, ",", &save))
    fields[n++] = tok;
  if (n < 4) {
    ESP_LOGW(TAG, "*IDN? -> '%s'", reply);
    return;
  }
  const char *firmware = strncmp(fields[3], "FV:", 3) == 0 ? fields[3] + 3 : fields[3];
  // Asked on every resync, so only news is worth INFO.
  if (this->serial_ != fields[2]) {
    this->serial_ = fields[2];
    ESP_LOGI(TAG, "%s %s, serial %s, firmware %s", fields[0], fields[1], fields[2], firmware);
  }
  this->publish_text_(TEXT_MODEL, std::string(fields[0]) + " " + fields[1]);
  this->publish_text_(TEXT_SERIAL, fields[2]);
  this->publish_text_(TEXT_FIRMWARE, firmware);
}

void OwonPSU::parse_info_(const char *reply) {
  // 24.290,0.005,0.120,OFF,OFF,OFF,1
  float v, a, w;
  char f[3][4];
  int mode;
  if (sscanf(reply, "%f,%f,%f,%3[^,],%3[^,],%3[^,],%d", &v, &a, &w, f[0], f[1], f[2], &mode) != 7) {
    ESP_LOGW(TAG, "MEAS:ALL:INFO? -> '%s'", reply);
    return;
  }
  this->publish_metric_(METRIC_VOLTAGE, v);
  this->publish_metric_(METRIC_CURRENT, a);
  this->publish_metric_(METRIC_POWER, w);
  for (uint8_t i = 0; i < 3; i++)
    this->publish_flag_(static_cast<Flag>(FLAG_OVP + i), strcmp(f[i], "ON") == 0);
  // 1 is seen in constant voltage. 2 appeared once with a 0 A setpoint and is
  // taken for constant current; neither has been checked against a load.
  this->publish_text_(TEXT_MODE, mode == 1 ? "CV" : mode == 2 ? "CC" : "mode " + std::to_string(mode));
}

void OwonPSU::on_read_back_(Setting s, float value) {
  this->actual_[s] = value;
  // A read-back that disagrees means the set was refused; the PSU handles
  // commands in order, so it cannot predate the set.
  if (!std::isnan(this->target_[s]) && this->sent_[s]) {
    if (std::fabs(value - this->target_[s]) < SETTING_TOLERANCE) {
      ESP_LOGI(TAG, "%s now %.3f", SETTING_NAME[s], value);
    } else {
      ESP_LOGW(TAG, "%s %.3f refused by the PSU, still %.3f", SETTING_NAME[s], this->target_[s], value);
    }
    this->target_[s] = NAN;
  }
  if (s == SETTING_OUTPUT) {
    if (this->output_switch_ != nullptr)
      this->output_switch_->publish_state(value > 0.5f);
  } else if (this->numbers_[s] != nullptr) {
    this->numbers_[s]->publish_state(value);
  }
}

void OwonPSU::check_link_() {
#ifdef USE_API
  // Home Assistant subscribes to states; `esphome logs` connects without, and
  // must not keep the output alive.
  const bool up = api::global_api_server != nullptr && api::global_api_server->is_connected_with_state_subscription();
  if (up) {
    if (this->link_trip_armed_)
      ESP_LOGI(TAG, "API back after %ums, output left alone", millis() - this->api_lost_at_);
    this->link_trip_armed_ = false;
    this->api_was_connected_ = true;
    return;
  }
  if (this->api_was_connected_) {
    this->api_was_connected_ = false;
    this->link_trip_armed_ = true;
    this->api_lost_at_ = millis();
    ESP_LOGW(TAG, "API lost; output goes off in %ums unless it returns", this->link_loss_timeout_);
  }
  if (!this->link_trip_armed_ || millis() - this->api_lost_at_ < this->link_loss_timeout_)
    return;
  this->link_trip_armed_ = false;
  if (this->actual_[SETTING_OUTPUT] == 0.0f) {
    ESP_LOGI(TAG, "API still lost; output already off");
    return;
  }
  // Not refused when the PSU is unreachable: it stays pending and goes out as
  // soon as the PSU answers again.
  ESP_LOGW(TAG, "API still lost; switching output off");
  this->target_[SETTING_OUTPUT] = 0.0f;
  this->sent_[SETTING_OUTPUT] = false;
#endif
}

void OwonPSU::set_connected_(bool connected) {
  if (connected == this->connected_)
    return;
  this->connected_ = connected;
  this->publish_flag_(FLAG_CONNECTED, connected);
  if (connected) {
    ESP_LOGI(TAG, "PSU answering");
    // Read every setting now; the resync already asked who this is.
    this->next_read_ = 0;
    this->last_settings_ = millis();
    return;
  }
  ESP_LOGW(TAG, "PSU not answering");
  for (uint8_t i = 0; i < METRIC_COUNT; i++)
    this->publish_metric_(static_cast<Metric>(i), NAN);
  // Whatever was sent may not have landed: send it again once it answers.
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    this->actual_[i] = NAN;
    this->sent_[i] = false;
  }
}

void OwonPSU::publish_metric_(Metric m, float value) {
  if (this->metric_sensors_[m] != nullptr)
    this->metric_sensors_[m]->publish_state(value);
}

void OwonPSU::publish_flag_(Flag f, bool state) {
  if (this->flag_sensors_[f] != nullptr)
    this->flag_sensors_[f]->publish_state(state);
}

void OwonPSU::publish_text_(Text t, const std::string &value) {
  if (this->text_sensors_[t] != nullptr)
    this->text_sensors_[t]->publish_state(value);
}

}  // namespace esphome::owon_psu

#endif
