#include "qx_ups.h"

#include "qx_protocol.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::qx_ups {

static const char *const TAG = "qx_ups";

// HID class request, as used by every Cypress-style bridge: the command goes
// out as output report 0 over the control pipe, because the bridge has no
// interrupt OUT endpoint.
static constexpr uint8_t HID_SET_REPORT = 0x09;
static constexpr uint16_t HID_REPORT_TYPE_OUTPUT = 0x0200;
// The bridge moves exactly one 8-byte report at a time, in both directions.
static constexpr size_t CHUNK = 8;


static const char *const COMMAND_TEXT[] = {nullptr, "Q1", "F", "I"};

void QxUPSClient::on_connected() {
  ESP_LOGI(TAG, "UPS bridge connected");
  this->interrupt_ep_ = 0;
  this->interrupt_pending_ = false;
  this->interface_claimed_ = false;
  this->rx_len_ = 0;
  this->reply_ready_ = false;
  this->rx_reset_ = false;
  this->in_flight_ = CMD_NONE;
  this->identity_pending_ = true;
  this->ratings_pending_ = true;
  this->identity_tries_ = 0;
  this->ratings_tries_ = 0;
  this->dialect_logged_ = false;
  this->battery_scale_logged_ = false;

  if (!this->discover_hid_interface_())
    return;

  // usb_host's USBClient never claims an interface, and a transfer cannot be
  // submitted to an endpoint whose interface is unclaimed -- so the interrupt
  // endpoint stays silent without this.
  const esp_err_t err = usb_host_interface_claim(this->handle_, this->device_handle_, this->hid_interface_, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "could not claim interface %u: %s", this->hid_interface_, esp_err_to_name(err));
    return;
  }
  this->interface_claimed_ = true;
  this->start_interrupt_in_();
}

void QxUPSClient::on_disconnected() {
  if (this->interface_claimed_) {
    usb_host_interface_release(this->handle_, this->device_handle_, this->hid_interface_);
    this->interface_claimed_ = false;
  }
  this->interrupt_ep_ = 0;
  this->in_flight_ = CMD_NONE;
  USBClient::on_disconnected();
}

bool QxUPSClient::discover_hid_interface_() {
  const usb_config_desc_t *config_desc;
  if (usb_host_get_active_config_descriptor(this->device_handle_, &config_desc) != ESP_OK) {
    ESP_LOGE(TAG, "no active configuration descriptor");
    return false;
  }

  for (uint8_t intf = 0; intf < config_desc->bNumInterfaces; intf++) {
    int offset = 0;
    const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf, 0, &offset);
    if (intf_desc == nullptr || intf_desc->bInterfaceClass != USB_CLASS_HID)
      continue;

    for (uint8_t i = 0; i < intf_desc->bNumEndpoints; i++) {
      int ep_offset = offset;
      const usb_ep_desc_t *ep =
          usb_parse_endpoint_descriptor_by_index(intf_desc, i, config_desc->wTotalLength, &ep_offset);
      if (ep == nullptr || (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_INT ||
          !(ep->bEndpointAddress & usb_host::USB_DIR_IN))
        continue;

      this->hid_interface_ = intf_desc->bInterfaceNumber;
      this->interrupt_ep_ = ep->bEndpointAddress;
      this->interrupt_mps_ = std::min<uint16_t>(ep->wMaxPacketSize, usb_host::USB_MAX_PACKET_SIZE);
      ESP_LOGI(TAG, "HID interface %u, interrupt IN 0x%02X, mps %u, interval %ums", this->hid_interface_,
               this->interrupt_ep_, this->interrupt_mps_, ep->bInterval);
      return true;
    }
  }

  ESP_LOGE(TAG, "no HID interrupt IN endpoint found");
  return false;
}

void QxUPSClient::start_interrupt_in_() {
  // Claim the subscription before submitting, so the USB task re-arming from a
  // callback and the main loop recovering a lost one cannot both submit.
  if (this->interrupt_ep_ == 0 || this->interrupt_pending_.exchange(true))
    return;

  const bool submitted = this->transfer_in(
      this->interrupt_ep_,
      // CALLBACK CONTEXT: USB task. Assemble only -- parsing runs in loop().
      [this](const usb_host::TransferStatus &status) {
        // A new command discards whatever a timed-out one left half-read.
        if (this->rx_reset_.exchange(false))
          this->rx_len_ = 0;
        if (status.success && status.data_len > 0 && !this->reply_ready_) {
          for (size_t i = 0; i < status.data_len; i++) {
            const char c = static_cast<char>(status.data[i]);
            if (c == '\r') {
              this->rx_buf_[this->rx_len_] = '\0';
              this->reply_ready_ = true;
              break;
            }
            if (this->rx_len_ + 1 < MAX_REPLY)
              this->rx_buf_[this->rx_len_++] = c;
          }
          if (!this->reply_ready_ && this->rx_len_ + 1 >= MAX_REPLY) {
            // No terminator in a whole buffer: the line is junk, and keeping it
            // would make every later reply junk too.
            this->rx_len_ = 0;
          }
        }
        // Stay armed. This is what keeps the bridge's 8-report buffer empty,
        // and an empty buffer is why a stale reply can never be read as the
        // next command's answer -- the desync NUT has to drain for.
        this->interrupt_pending_ = false;
        this->start_interrupt_in_();
      },
      this->interrupt_mps_);

  if (!submitted) {
    // The subscription is gone until something resubmits, so let loop() retry
    // instead of leaving the endpoint permanently silent after one bad moment.
    this->interrupt_pending_ = false;
    ESP_LOGW(TAG, "could not submit interrupt read on 0x%02X", this->interrupt_ep_);
  }
}

void QxUPSClient::send_command_(Command cmd) {
  const char *text = COMMAND_TEXT[cmd];
  this->tx_len_ = snprintf(reinterpret_cast<char *>(this->tx_buf_), MAX_COMMAND, "%s\r", text);
  this->tx_off_ = 0;
  this->rx_reset_ = true;
  this->in_flight_ = cmd;
  this->command_started_ = millis();
  this->send_next_chunk_();
}

void QxUPSClient::send_next_chunk_() {
  // The bridge accepts whole 8-byte reports only; a short write is discarded.
  std::vector<uint8_t> chunk(CHUNK, 0);
  const size_t n = std::min(CHUNK, this->tx_len_ - this->tx_off_);
  memcpy(chunk.data(), this->tx_buf_ + this->tx_off_, n);
  this->tx_off_ += n;

  const bool submitted = this->control_transfer(
      usb_host::USB_DIR_OUT | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE, HID_SET_REPORT,
      HID_REPORT_TYPE_OUTPUT, this->hid_interface_,
      // CALLBACK CONTEXT: USB task.
      [this](const usb_host::TransferStatus &status) {
        if (!status.success) {
          ESP_LOGW(TAG, "command write failed (error %u)", status.error_code);
          this->in_flight_ = CMD_NONE;
          return;
        }
        if (this->tx_off_ < this->tx_len_)
          this->send_next_chunk_();
      },
      chunk);

  if (!submitted) {
    ESP_LOGW(TAG, "could not submit command write");
    this->in_flight_ = CMD_NONE;
  }
}

void QxUPSClient::loop() {
  const bool had_events = this->process_usb_events_();
  if (this->state_ != usb_host::USB_CLIENT_CONNECTED || this->interrupt_ep_ == 0) {
    if (!had_events)
      this->disable_loop();
    return;
  }

  // Recover a subscription that a transient submit failure dropped.
  this->start_interrupt_in_();

  if (this->reply_ready_) {
    const Command answered = this->in_flight_.exchange(CMD_NONE);
    if (answered == CMD_NONE) {
      // A reply to a command we already gave up on. Dropping it here is the
      // whole desync fix: it is never attributed to the next command.
      ESP_LOGD(TAG, "dropped late reply: %s", this->rx_buf_);
    } else {
      ESP_LOGV(TAG, "%s -> %s (%ums)", COMMAND_TEXT[answered], this->rx_buf_, millis() - this->command_started_);
      this->on_reply_(answered, this->rx_buf_);
    }
    // Safe here and nowhere else: reply_ready_ keeps the USB task off the
    // buffer until it is cleared, which is the last thing done.
    this->rx_len_ = 0;
    this->reply_ready_ = false;
  }

  const Command waiting = this->in_flight_.load();
  if (waiting != CMD_NONE) {
    if (millis() - this->command_started_ > this->reply_timeout_) {
      ESP_LOGW(TAG, "no reply to %s within %ums", COMMAND_TEXT[waiting], this->reply_timeout_);
      this->in_flight_ = CMD_NONE;
      if (waiting == CMD_IDENTITY && ++this->identity_tries_ >= MAX_QUERY_TRIES) {
        ESP_LOGW(TAG, "giving up on I; this UPS does not report its identity");
        this->identity_pending_ = false;
      } else if (waiting == CMD_RATINGS && ++this->ratings_tries_ >= MAX_QUERY_TRIES) {
        ESP_LOGW(TAG, "giving up on F; charge needs an explicit battery voltage range");
        this->ratings_pending_ = false;
      }
    }
    return;
  }

  // Status first, always: the nameplate queries are one-offs, and a UPS that
  // answers them slowly or not at all must not delay a mains-loss reading.
  // They fill the gap between status polls instead.
  if (millis() - this->last_status_ >= this->status_interval_) {
    this->last_status_ = millis();
    this->send_command_(CMD_STATUS);
  } else if (this->identity_pending_) {
    this->send_command_(CMD_IDENTITY);
  } else if (this->ratings_pending_) {
    this->send_command_(CMD_RATINGS);
  }
}

void QxUPSClient::on_reply_(Command answered, const char *reply) {
  // Dispatch on what was asked, never on the reply: `F` and `I` both answer
  // with a leading '#', so the text cannot tell them apart.
  if (answered == CMD_STATUS) {
    if (!this->parse_status_(reply))
      ESP_LOGW(TAG, "unparsed status reply: %s", reply);
    return;
  }

  // Asked once either way: a UPS that does not implement the command answers
  // junk, and retrying it forever would starve the status poll.
  if (answered == CMD_RATINGS) {
    this->parse_ratings_(reply);
    this->ratings_pending_ = false;
  } else {
    this->parse_identity_(reply);
    this->identity_pending_ = false;
  }
}

bool QxUPSClient::parse_status_(const char *reply) {
  QxStatus status;
  if (!qx_parse_status(reply, &status))
    return false;

  if (!this->dialect_logged_) {
    ESP_LOGI(TAG, "Megatec Q1 accepted: %s", reply);
    this->dialect_logged_ = true;
  }

  static_assert(static_cast<int>(QX_FIELD_COUNT) == static_cast<int>(METRIC_TEMPERATURE) + 1,
                "Metric must open with the Q1 fields, in protocol order");

  // 2.27 V means nothing to a reader; 27.2 V on a 24 V pack does.
  const QxBattery battery = qx_scale_battery(status.value[QX_BATTERY_VOLTAGE], this->rated_battery_v_);
  status.value[QX_BATTERY_VOLTAGE] = battery.voltage;
  if (!this->battery_scale_logged_) {
    ESP_LOGI(TAG, "battery reported %s; nominal %.1f V, reading %.2f V", battery.per_cell ? "per cell" : "per pack",
             battery.nominal, battery.voltage);
    this->battery_scale_logged_ = true;
  }

  for (uint8_t i = 0; i < QX_FIELD_COUNT; i++)
    this->publish_metric_(static_cast<Metric>(i), status.value[i]);

  const bool utility_fail = status.bit[QX_UTILITY_FAIL];
  this->publish_flag_(FLAG_ONLINE, !utility_fail);
  this->publish_flag_(FLAG_ON_BATTERY, utility_fail);
  this->publish_flag_(FLAG_LOW_BATTERY, status.bit[QX_BATTERY_LOW]);
  this->publish_flag_(FLAG_BOOST_BUCK, status.bit[QX_BOOST_BUCK]);
  this->publish_flag_(FLAG_UPS_FAILED, status.bit[QX_UPS_FAILED]);
  this->publish_flag_(FLAG_TEST_IN_PROGRESS, status.bit[QX_TEST_IN_PROGRESS]);
  this->publish_flag_(FLAG_SHUTDOWN_ACTIVE, status.bit[QX_SHUTDOWN_ACTIVE]);
  this->publish_flag_(FLAG_BEEPER_ON, status.bit[QX_BEEPER_ON]);

  if (!std::isnan(this->nominal_power_))
    this->publish_metric_(METRIC_POWER, status.value[QX_LOAD] * this->nominal_power_ / 100.0f);

  // An explicit range in YAML overrides the estimate, for a pack whose
  // chemistry or wear makes the conventional thresholds wrong.
  float charge = NAN;
  if (!std::isnan(this->battery_low_v_) && this->battery_high_v_ > this->battery_low_v_) {
    charge = clamp((battery.voltage - this->battery_low_v_) /
                       (this->battery_high_v_ - this->battery_low_v_) * 100.0f,
                   0.0f, 100.0f);
  } else {
    charge = qx_charge_percent(battery.voltage, battery.nominal);
  }
  if (!std::isnan(charge))
    this->publish_metric_(METRIC_BATTERY_LEVEL, charge);
  return true;
}

void QxUPSClient::parse_ratings_(const char *reply) {
  QxRatings ratings;
  if (!qx_parse_ratings(reply, &ratings)) {
    ESP_LOGW(TAG, "unparsed ratings reply: %s", reply);
    return;
  }

  // Some units report the pack voltage per cell. Only a plausible pack voltage
  // may size the charge estimate; a wrong one would read as a flat battery.
  if (ratings.battery_voltage >= 6.0f && ratings.battery_voltage <= 300.0f) {
    this->rated_battery_v_ = ratings.battery_voltage;
  } else {
    ESP_LOGW(TAG, "implausible rated battery voltage %.2f V, charge estimate needs an explicit range",
             ratings.battery_voltage);
  }
  ESP_LOGI(TAG, "ratings: %.1f V, %.0f A, %.2f V battery, %.1f Hz", ratings.voltage, ratings.current,
           ratings.battery_voltage, ratings.frequency);
  // Published as well as logged: the exchange happens within a second of
  // enumeration, long before a log client can attach to watch it.
  this->publish_metric_(METRIC_RATED_VOLTAGE, ratings.voltage);
  this->publish_metric_(METRIC_RATED_CURRENT, ratings.current);
  this->publish_metric_(METRIC_RATED_BATTERY_VOLTAGE, ratings.battery_voltage);
}

void QxUPSClient::parse_identity_(const char *reply) {
  QxIdentity identity;
  if (!qx_parse_identity(reply, &identity)) {
    ESP_LOGW(TAG, "unparsed identity reply: %s", reply);
    return;
  }

  const char *value[TEXT_COUNT] = {identity.manufacturer, identity.model, identity.firmware};
  const char *label[TEXT_COUNT] = {"manufacturer", "model", "firmware"};
  for (uint8_t i = 0; i < TEXT_COUNT; i++) {
    if (value[i][0] == '\0')
      continue;
    ESP_LOGI(TAG, "%s: %s", label[i], value[i]);
    if (this->text_sensors_[i] != nullptr)
      this->text_sensors_[i]->publish_state(value[i]);
  }
}

void QxUPSClient::publish_metric_(Metric m, float value) {
  if (this->metric_sensors_[m] != nullptr && this->metric_sensors_[m]->get_raw_state() != value)
    this->metric_sensors_[m]->publish_state(value);
}

void QxUPSClient::publish_flag_(Flag f, bool state) {
  if (this->flag_sensors_[f] != nullptr)
    this->flag_sensors_[f]->publish_state(state);
}

void QxUPSClient::dump_config() {
  ESP_LOGCONFIG(TAG, "Megatec/Q* UPS:");
  ESP_LOGCONFIG(TAG, "  Status interval: %ums", this->status_interval_);
  ESP_LOGCONFIG(TAG, "  Reply timeout: %ums", this->reply_timeout_);
  if (!std::isnan(this->nominal_power_))
    ESP_LOGCONFIG(TAG, "  Nominal power: %.0f W", this->nominal_power_);
  if (this->interrupt_ep_ != 0)
    ESP_LOGCONFIG(TAG, "  Interface %u, interrupt IN 0x%02X, mps %u", this->hid_interface_, this->interrupt_ep_,
                  this->interrupt_mps_);
  USBClient::dump_config();
}

}  // namespace esphome::qx_ups

#endif
