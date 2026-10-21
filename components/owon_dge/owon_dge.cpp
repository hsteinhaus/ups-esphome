// SPDX-License-Identifier: GPL-2.0-only
#include "owon_dge.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include <cmath>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::owon_dge {

static const char *const TAG = "owon_dge";

static constexpr uint8_t EP_OUT = 0x01;
static constexpr uint8_t EP_IN = 0x81;
// Full speed caps bulk packets at 64 bytes.
static constexpr uint16_t FS_BULK_MPS = 64;
static constexpr uint8_t MAX_MISSED_REPLIES = 3;
// Longer than any reply is expected to take. After a miss the line must be
// silent this long before the resync *IDN? goes out, so no earlier reply is
// still on its way; the *IDN? is then the only query outstanding and is given
// as long to answer. As owon_psu, whose PSU takes up to ~1 s.
static constexpr uint32_t RESYNC_QUIET_MS = 1500;

// Polled every status interval: what the front panel most often changes, and
// the exclusive mode states, which one set can flip as a side effect.
static const Setting STATUS[] = {
    SETTING_OUTPUT,   SETTING_SHAPE,    SETTING_FREQUENCY,   SETTING_AMPLITUDE,   SETTING_OFFSET,
    SETTING_AM_STATE, SETTING_FM_STATE, SETTING_FSK_STATE, SETTING_SWEEP_STATE, SETTING_BURST_STATE,
};
static constexpr uint8_t STATUS_COUNT = sizeof(STATUS) / sizeof(STATUS[0]);
static const Setting MODULATION_STATE[] = {SETTING_AM_STATE, SETTING_FM_STATE, SETTING_FSK_STATE};

// The unit echoes six significant digits; a set that reads back within that
// has landed.
static bool matches(float want, float got) {
  if (std::isinf(want) || std::isinf(got))
    return std::isinf(want) && std::isinf(got);
  return std::fabs(want - got) <= 1e-5f * std::fmax(std::fabs(want), 1e-6f) + 1e-12f;
}

void OwonDGENumber::control(float value) { this->parent_->request_setting(this->setting_, value); }

void OwonDGESelect::control(size_t index) {
  if (this->setting_ == SETTING_COUNT) {
    this->parent_->request_modulation(static_cast<Modulation>(index));
  } else {
    this->parent_->request_setting(this->setting_, index);
  }
}

void OwonDGESwitch::write_state(bool state) {
  if (this->setting_ == SETTING_LOAD) {
    this->parent_->request_setting(SETTING_LOAD, state ? INFINITY : 50.0f);
  } else {
    this->parent_->request_setting(this->setting_, state ? 1.0f : 0.0f);
  }
}

void OwonDGE::setup() {
  USBClient::setup();
  for (uint8_t i = 0; i < SETTING_COUNT; i++)
    this->target_[i] = NAN;
  this->reset_state_();
  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_state(false);
}

void OwonDGE::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Owon DGE:\n"
                "  Status interval: %ums\n"
                "  Settings interval: %ums\n"
                "  Reply timeout: %ums",
                this->status_interval_, this->settings_interval_, this->reply_timeout_);
}

void OwonDGE::reset_state_() {
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    this->actual_[i] = NAN;
    this->sent_[i] = false;
  }
  this->in_flight_ = QUERY_NONE;
  this->line_len_ = 0;
  this->resync_ = true;
  this->status_next_ = 0xFF;
  this->round_next_ = 0;
}

void OwonDGE::on_connected() {
  // The firmware describes its bulk pipes with their high-speed 512, which IDF
  // refuses at full speed. Linux clamps the same way, and the unit then sends
  // full-speed packets.
  const usb_config_desc_t *cfg;
  if (usb_host_get_active_config_descriptor(this->device_handle_, &cfg) == ESP_OK) {
    int offset = 0;
    const auto *d = reinterpret_cast<const usb_standard_desc_t *>(cfg);
    while ((d = usb_parse_next_descriptor_of_type(d, cfg->wTotalLength, USB_B_DESCRIPTOR_TYPE_ENDPOINT, &offset))) {
      // IDF keeps this copy in RAM and reads it when the interface is claimed.
      auto *ep = const_cast<usb_ep_desc_t *>(reinterpret_cast<const usb_ep_desc_t *>(d));
      if (ep->wMaxPacketSize > FS_BULK_MPS)
        ep->wMaxPacketSize = FS_BULK_MPS;
    }
  }
  const esp_err_t err = usb_host_interface_claim(this->handle_, this->device_handle_, 0, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "claiming interface 0 failed: %s", esp_err_to_name(err));
    this->status_set_error(LOG_STR("interface claim failed"));
    return;
  }
  ESP_LOGI(TAG, "generator on USB");
  this->status_clear_error();
  this->reset_state_();
  this->in_pending_ = false;
  this->in_failed_ = false;
  this->rx_ready_ = false;
  this->claimed_ = true;
  this->start_input_();
}

void OwonDGE::on_disconnected() {
  // Also called for every device found not to be ours.
  if (this->claimed_) {
    for (uint8_t ep : {EP_IN, EP_OUT}) {
      usb_host_endpoint_halt(this->device_handle_, ep);
      usb_host_endpoint_flush(this->device_handle_, ep);
    }
    // Held, the interface keeps the device from closing, and its channels taken.
    usb_host_interface_release(this->handle_, this->device_handle_, 0);
    this->claimed_ = false;
    this->set_connected_(false);
    this->reset_state_();
  }
  USBClient::on_disconnected();
}

void OwonDGE::suspend_usb() {
  this->suspended_ = true;
  this->in_flight_ = QUERY_NONE;
  // Bulk pipes halt synchronously; the flush completes what is queued as
  // cancelled, and the IN callback sees suspended_ and does not re-arm.
  if (this->claimed_) {
    for (uint8_t ep : {EP_IN, EP_OUT}) {
      usb_host_endpoint_halt(this->device_handle_, ep);
      usb_host_endpoint_flush(this->device_handle_, ep);
    }
  }
}

void OwonDGE::resume_usb() {
  if (this->claimed_) {
    for (uint8_t ep : {EP_IN, EP_OUT})
      usb_host_endpoint_clear(this->device_handle_, ep);
  }
  this->in_pending_ = false;
  this->in_failed_ = false;
  this->rx_ready_ = false;
  this->line_len_ = 0;
  this->resync_ = true;
  // Anything that lands around the halt counts as the line still busy.
  this->last_rx_ = millis();
  this->suspended_ = false;
}

void OwonDGE::start_input_() {
  if (!this->claimed_ || this->suspended_ || this->rx_ready_ || this->in_failed_ || this->in_pending_.exchange(true))
    return;
  const bool ok = this->transfer_in(
      EP_IN,
      [this](const usb_host::TransferStatus &st) {
        // USB task: hand the bytes over, the main loop re-arms.
        if (st.success && !this->suspended_) {
          const size_t n = std::min(st.data_len, sizeof(this->rx_chunk_));
          memcpy(this->rx_chunk_, st.data, n);
          this->rx_chunk_len_.store(n);
          this->rx_ready_.store(true);
        } else if (!st.success && !this->suspended_) {
          this->in_failed_.store(true);
        }
        this->in_pending_.store(false);
      },
      FS_BULK_MPS);
  if (!ok)
    this->in_pending_ = false;
}

void OwonDGE::loop() {
  this->process_usb_events_();
  if (!this->claimed_ || this->suspended_)
    return;

  if (this->rx_ready_.load()) {
    const uint16_t n = this->rx_chunk_len_.load();
    for (uint16_t i = 0; i < n; i++) {
      const char c = static_cast<char>(this->rx_chunk_[i]);
      if (c == '\r')
        continue;
      if (c != '\n') {
        // An overlong line is cut, not wrapped into the next reply.
        if (this->line_len_ < MAX_LINE - 1)
          this->line_[this->line_len_++] = c;
        continue;
      }
      this->line_[this->line_len_] = '\0';
      this->line_len_ = 0;
      this->on_line_(this->line_);
    }
    this->last_rx_ = millis();
    this->rx_ready_.store(false);
  }
  this->start_input_();

  uint32_t now = millis();
  if (this->in_flight_ != QUERY_NONE) {
    const uint32_t timeout = this->resync_ ? RESYNC_QUIET_MS : this->reply_timeout_;
    if (now - this->command_started_ < timeout)
      return;
    this->on_timeout_();
    // on_timeout_ restarts the quiet window at its own millis().
    now = millis();
  }

  // Replies carry nothing that names their query, so a late one would answer
  // the next query instead. After a miss nothing is asked until the line has
  // been quiet for RESYNC_QUIET_MS, then only *IDN?, until its reply returns.
  if (this->resync_) {
    if (now - this->last_rx_ >= RESYNC_QUIET_MS)
      this->send_query_(QUERY_IDENTITY);
    return;
  }

  // Pending sets first, then read-backs that have not matched yet: the unit
  // can apply a set a few queries late (AM:STAT ON did).
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    if (!std::isnan(this->target_[i]) && !this->sent_[i]) {
      this->send_setting_(static_cast<Setting>(i));
      return;
    }
  }
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    if (std::isnan(this->target_[i]) || !this->sent_[i])
      continue;
    if (now - this->sent_at_[i] >= this->settings_interval_) {
      ESP_LOGW(TAG, "%s: set not taken, reads %g", setting_def(static_cast<Setting>(i)).scpi, this->actual_[i]);
      this->target_[i] = NAN;
      this->sent_[i] = false;
      this->publish_setting_(static_cast<Setting>(i));
      continue;
    }
    this->send_query_(i);
    return;
  }

  if (now - this->last_status_ >= this->status_interval_) {
    this->last_status_ = now;
    this->status_next_ = 0;
  }
  if (this->status_next_ < STATUS_COUNT) {
    this->send_query_(STATUS[this->status_next_++]);
    return;
  }
  // Everything else, one query per slot, so a full round takes the settings
  // interval.
  if (now - this->last_round_ >= this->settings_interval_ / SETTING_COUNT) {
    this->last_round_ = now;
    this->send_query_(this->round_next_);
    this->round_next_ = (this->round_next_ + 1) % SETTING_COUNT;
  }
}

void OwonDGE::send_line_(const char *line) {
  // transfer_out copies, so the buffer need not outlive the call.
  char tx[MAX_LINE];
  const int n = snprintf(tx, sizeof(tx), "%s\n", line);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(tx) || n > FS_BULK_MPS)
    return;
  ESP_LOGV(TAG, "-> '%s'", line);
  this->transfer_out(
      EP_OUT,
      [](const usb_host::TransferStatus &st) {
        if (!st.success)
          ESP_LOGW(TAG, "OUT failed: %u", st.error_code);
      },
      reinterpret_cast<const uint8_t *>(tx), n);
}

void OwonDGE::send_query_(uint8_t query) {
  // A failed IN is retried once per query, not per loop: a pipe that keeps
  // failing then costs a reply timeout each round, not a busy loop.
  if (this->in_failed_.exchange(false)) {
    ESP_LOGW(TAG, "IN transfer failed, clearing the pipe");
    // IDF clears only a halted pipe.
    usb_host_endpoint_halt(this->device_handle_, EP_IN);
    usb_host_endpoint_flush(this->device_handle_, EP_IN);
    usb_host_endpoint_clear(this->device_handle_, EP_IN);
    this->start_input_();
  }
  char buf[48];
  if (query == QUERY_IDENTITY) {
    strcpy(buf, "*IDN?");
  } else if (!format_query(static_cast<Setting>(query), buf, sizeof(buf))) {
    return;
  }
  this->send_line_(buf);
  this->in_flight_ = query;
  this->command_started_ = millis();
}

void OwonDGE::send_setting_(Setting s) {
  char buf[48];
  if (!format_set(s, this->target_[s], buf, sizeof(buf))) {
    ESP_LOGW(TAG, "%s: %g cannot be sent", setting_def(s).scpi, this->target_[s]);
    this->target_[s] = NAN;
    this->publish_setting_(s);
    return;
  }
  this->send_line_(buf);
  this->sent_[s] = true;
  this->sent_at_[s] = millis();
  // The set itself is never answered; the query after it is, in order.
  this->send_query_(s);
}

void OwonDGE::on_line_(const char *line) {
  if (this->resync_) {
    if (this->in_flight_ != QUERY_IDENTITY || !is_identity_reply(line)) {
      ESP_LOGD(TAG, "resyncing, dropped '%s'", line);
      return;
    }
    this->resync_ = false;
  }
  const uint8_t answered = this->in_flight_;
  this->in_flight_ = QUERY_NONE;
  if (answered == QUERY_NONE) {
    ESP_LOGW(TAG, "unsolicited reply '%s'", line);
    return;
  }
  this->missed_replies_ = 0;
  if (answered == QUERY_IDENTITY) {
    this->parse_identity_(line);
    this->set_connected_(true);
    return;
  }
  const auto s = static_cast<Setting>(answered);
  const float value = parse_reply(s, line);
  if (std::isnan(value)) {
    ESP_LOGW(TAG, "%s? -> '%s'", setting_def(s).scpi, line);
    return;
  }
  this->on_read_back_(s, value);
}

void OwonDGE::on_timeout_() {
  ESP_LOGD(TAG, "query %u unanswered", this->in_flight_);
  this->in_flight_ = QUERY_NONE;
  this->line_len_ = 0;
  this->resync_ = true;
  // The quiet window before the next *IDN? starts now.
  this->last_rx_ = millis();
  if (++this->missed_replies_ >= MAX_MISSED_REPLIES)
    this->set_connected_(false);
}

void OwonDGE::parse_identity_(const char *reply) {
  // OWON,DGE1060,24500065,SCPI:99.0 FV:V1.0.4.4.0
  char buf[MAX_LINE];
  strncpy(buf, reply, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char *save;
  const char *vendor = strtok_r(buf, ",", &save);
  const char *model = strtok_r(nullptr, ",", &save);
  const char *serial = strtok_r(nullptr, ",", &save);
  const char *version = strtok_r(nullptr, ",", &save);
  if (vendor == nullptr || model == nullptr || serial == nullptr)
    return;
  const char *fw = version != nullptr ? strstr(version, "FV:") : nullptr;
  const std::string model_text = std::string(vendor) + " " + model;
  if (this->text_sensors_[TEXT_MODEL] != nullptr)
    this->text_sensors_[TEXT_MODEL]->publish_state(model_text);
  if (this->text_sensors_[TEXT_SERIAL] != nullptr)
    this->text_sensors_[TEXT_SERIAL]->publish_state(serial);
  if (this->text_sensors_[TEXT_FIRMWARE] != nullptr)
    this->text_sensors_[TEXT_FIRMWARE]->publish_state(fw != nullptr ? fw + 3 : (version != nullptr ? version : ""));
}

void OwonDGE::request_setting(Setting s, float value) {
  if (!this->connected_) {
    ESP_LOGW(TAG, "%s %g refused: generator not connected", setting_def(s).scpi, value);
    this->publish_setting_(s);
    return;
  }
  ESP_LOGI(TAG, "%s -> %g requested", setting_def(s).scpi, value);
  this->target_[s] = value;
  this->sent_[s] = false;
}

void OwonDGE::request_modulation(Modulation m) {
  if (m == MODULATION_OFF) {
    // Only the type that is on, or about to be: turning one off never turns
    // another on.
    for (Setting s : MODULATION_STATE) {
      if (this->actual_[s] > 0.5f || this->target_[s] > 0.5f)
        this->request_setting(s, 0.0f);
    }
    if (!this->connected_)
      this->publish_modulation_();
    return;
  }
  // The unit turns the other types (and sweep, burst) off itself.
  this->request_setting(MODULATION_STATE[m - MODULATION_AM], 1.0f);
}

void OwonDGE::on_read_back_(Setting s, float value) {
  this->actual_[s] = value;
  if (!std::isnan(this->target_[s]) && this->sent_[s] && matches(this->target_[s], value)) {
    ESP_LOGI(TAG, "%s now %g", setting_def(s).scpi, value);
    this->target_[s] = NAN;
    this->sent_[s] = false;
  }
  // A read-back still disagreeing with a fresh set is not published yet: it
  // may predate the set taking effect, and would flick the entity back.
  if (std::isnan(this->target_[s]))
    this->publish_setting_(s);
}

void OwonDGE::publish_setting_(Setting s) {
  const float v = this->actual_[s];
  if (this->numbers_[s] != nullptr) {
    // The load number shows ohms; High-Z is the switch's.
    this->numbers_[s]->publish_state(std::isinf(v) ? NAN : v);
  }
  if (this->switches_[s] != nullptr && !std::isnan(v))
    this->switches_[s]->publish_state(s == SETTING_LOAD ? std::isinf(v) : v > 0.5f);
  if (this->selects_[s] != nullptr && !std::isnan(v))
    this->selects_[s]->publish_state(static_cast<size_t>(v));
  for (Setting m : MODULATION_STATE) {
    if (s == m) {
      this->publish_modulation_();
      break;
    }
  }
}

void OwonDGE::publish_modulation_() {
  if (this->modulation_select_ == nullptr)
    return;
  size_t active = MODULATION_OFF;
  for (uint8_t i = 0; i < 3; i++) {
    const float v = this->actual_[MODULATION_STATE[i]];
    if (std::isnan(v))
      return;
    if (v > 0.5f)
      active = MODULATION_AM + i;
  }
  this->modulation_select_->publish_state(active);
}

void OwonDGE::set_connected_(bool connected) {
  if (connected == this->connected_)
    return;
  this->connected_ = connected;
  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_state(connected);
  if (connected) {
    ESP_LOGI(TAG, "generator answering");
    // Read everything now.
    this->status_next_ = 0;
    this->round_next_ = 0;
    return;
  }
  ESP_LOGW(TAG, "generator not answering");
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    this->target_[i] = NAN;
    this->sent_[i] = false;
  }
}

}  // namespace esphome::owon_dge

#endif
