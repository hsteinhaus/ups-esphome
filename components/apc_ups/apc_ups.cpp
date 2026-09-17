#include "apc_ups.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>

namespace esphome::apc_ups {

static const char *const TAG = "apc_ups";

// GET_DESCRIPTOR(HID report): standard IN request on the interface, descriptor
// type 0x22 in the high byte of wValue.
static constexpr uint8_t REQ_GET_DESCRIPTOR = 0x06;
static constexpr uint16_t DESC_TYPE_HID_REPORT = 0x2200;

// HID class GET_REPORT, report type 3 (feature) in the high byte of wValue.
static constexpr uint8_t REQ_GET_REPORT = 0x01;
static constexpr uint16_t REPORT_TYPE_FEATURE = 0x0300;

static constexpr uint16_t HID_INTERFACE = 0;

// The device exposes a single interface with one interrupt IN endpoint; see
// docs/bx950mi-hid-map.md.
static constexpr uint8_t EP_INTERRUPT_IN = 0x81;
static constexpr uint16_t REPORT_READ_LEN = 16;

// usb_host raises the control transfer limit to max_packet_size minus the
// 8-byte setup packet; the descriptor must arrive in one transfer because
// GET_DESCRIPTOR has no offset parameter.
static constexpr uint16_t DESCRIPTOR_REQUEST_LEN = 1024 - 8;

// Dumping the whole field table at once overruns the log buffer.
static constexpr int FIELDS_PER_LOOP = 4;

// A control transfer's callback hands back the whole buffer, setup packet
// included, and data_len counts it. Decoding from data[0] reads the request
// header as if it were report data: it made PercentLoad read 161 (0xA1, the
// bmRequestType byte) and both voltages read raw 417 (0x01A1). Interrupt
// transfers have no setup packet and need no offset.
static constexpr size_t SETUP_PACKET_SIZE = 8;

// Usage page constants, matching hid_pdc.h.
static constexpr uint16_t PAGE_POWER = HID_PAGE_POWER_DEVICE;
static constexpr uint16_t PAGE_BATTERY = HID_PAGE_BATTERY;

// Collections
static constexpr uint16_t U_PRESENT_STATUS = 0x0002;
static constexpr uint16_t U_BATTERY = 0x0012;
static constexpr uint16_t U_POWER_CONVERTER = 0x0016;
static constexpr uint16_t U_INPUT = 0x001A;
static constexpr uint16_t U_POWER_SUMMARY = 0x0024;

struct FieldSpec {
  uint16_t ancestor_page;
  uint16_t ancestor_id;
  uint16_t leaf_page;
  uint16_t leaf_id;
};

// The same leaf usage means different things under different collections, so
// each entry names its ancestor: Voltage (0x84:0x30) appears under Input,
// Battery, Output and PowerSummary.
static const FieldSpec METRIC_SPECS[METRIC_COUNT] = {
    {PAGE_POWER, U_POWER_SUMMARY, PAGE_BATTERY, 0x0066},   // RemainingCapacity
    {PAGE_POWER, U_POWER_SUMMARY, PAGE_BATTERY, 0x0068},   // RunTimeToEmpty
    {PAGE_POWER, U_INPUT, PAGE_POWER, 0x0030},             // Input.Voltage
    {PAGE_POWER, U_BATTERY, PAGE_POWER, 0x0030},           // Battery.Voltage
    {PAGE_POWER, U_POWER_CONVERTER, PAGE_POWER, 0x0035},   // PercentLoad
    {PAGE_POWER, U_POWER_CONVERTER, PAGE_POWER, 0x0044},   // ConfigActivePower
    {0, 0, 0, 0},                                          // METRIC_POWER: derived
};

static const FieldSpec FLAG_SPECS[FLAG_COUNT] = {
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_BATTERY, 0x00D0},  // ACPresent
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_BATTERY, 0x0044},  // Charging
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_BATTERY, 0x0045},  // Discharging
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_BATTERY, 0x0042},  // BelowRemainingCapacityLimit
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_BATTERY, 0x004B},  // NeedReplacement
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_POWER, 0x0065},    // Overload
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_POWER, 0x0069},    // ShutdownImminent
    {PAGE_POWER, U_PRESENT_STATUS, PAGE_BATTERY, 0x00D1},  // BatteryPresent
};

void APCUPSClient::on_connected() {
  ESP_LOGI(TAG, "UPS connected, requesting HID report descriptor");
  this->bound_ = false;
  this->log_index_ = -1;
  this->descriptor_ready_ = false;
  this->descriptor_len_ = 0;
  this->feature_report_count_ = 0;

  // usb_host's USBClient never claims an interface, and a transfer cannot be
  // submitted to an endpoint whose interface is unclaimed -- so the interrupt
  // endpoint stays silent without this. usb_uart does the same by hand.
  const esp_err_t err = usb_host_interface_claim(this->handle_, this->device_handle_, HID_INTERFACE, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "could not claim interface %u: %s", HID_INTERFACE, esp_err_to_name(err));
  } else {
    this->interface_claimed_ = true;
  }

  this->request_report_descriptor_();
}

void APCUPSClient::on_disconnected() {
  ESP_LOGI(TAG, "UPS disconnected");
  this->bound_ = false;
  this->log_index_ = -1;
  this->descriptor_ready_ = false;
  this->feature_report_count_ = 0;

  if (this->interface_claimed_) {
    usb_host_endpoint_halt(this->device_handle_, EP_INTERRUPT_IN);
    usb_host_endpoint_flush(this->device_handle_, EP_INTERRUPT_IN);
    usb_host_interface_release(this->handle_, this->device_handle_, HID_INTERFACE);
    this->interface_claimed_ = false;
  }

  USBClient::on_disconnected();
}

void APCUPSClient::request_report_descriptor_() {
  const uint8_t type = usb_host::USB_DIR_IN | usb_host::USB_TYPE_STANDARD | usb_host::USB_RECIP_INTERFACE;
  const std::vector<uint8_t> read_buffer(DESCRIPTOR_REQUEST_LEN);

  const bool submitted = this->control_transfer(
      type, REQ_GET_DESCRIPTOR, DESC_TYPE_HID_REPORT, HID_INTERFACE,
      // CALLBACK CONTEXT: USB task. Copy and hand over; do not parse here.
      [this](const usb_host::TransferStatus &status) {
        if (!status.success || status.data_len <= SETUP_PACKET_SIZE) {
          ESP_LOGE(TAG, "report descriptor request failed (err=%u, len=%u)", status.error_code, status.data_len);
          return;
        }
        const size_t len = std::min(status.data_len - SETUP_PACKET_SIZE, MAX_DESCRIPTOR);
        memcpy(this->descriptor_, status.data + SETUP_PACKET_SIZE, len);
        this->descriptor_len_ = len;
        this->descriptor_ready_ = true;
        // USBClient::loop() disables the loop once the bus goes idle, so the
        // main loop would never see this flag without being woken here.
        this->enable_loop_soon_any_context();
      },
      read_buffer);

  if (!submitted)
    ESP_LOGE(TAG, "could not submit report descriptor request");
}

void APCUPSClient::bind_fields_() {
  size_t bound = 0;

  for (uint8_t i = 0; i < METRIC_COUNT; i++) {
    const FieldSpec &s = METRIC_SPECS[i];
    if (s.leaf_id == 0)
      continue;  // derived, not backed by a descriptor field
    this->metric_input_[i] =
        hid_pdc_find(&this->map_, HID_PDC_INPUT, s.ancestor_page, s.ancestor_id, s.leaf_page, s.leaf_id);
    this->metric_feature_[i] =
        hid_pdc_find(&this->map_, HID_PDC_FEATURE, s.ancestor_page, s.ancestor_id, s.leaf_page, s.leaf_id);
    if (this->metric_sensors_[i] != nullptr && this->metric_input_[i] == nullptr &&
        this->metric_feature_[i] == nullptr) {
      ESP_LOGW(TAG, "metric %u not present in this UPS's descriptor", i);
    } else if (this->metric_sensors_[i] != nullptr) {
      bound++;
    }
  }

  for (uint8_t i = 0; i < FLAG_COUNT; i++) {
    const FieldSpec &s = FLAG_SPECS[i];
    this->flag_input_[i] =
        hid_pdc_find(&this->map_, HID_PDC_INPUT, s.ancestor_page, s.ancestor_id, s.leaf_page, s.leaf_id);
    this->flag_feature_[i] =
        hid_pdc_find(&this->map_, HID_PDC_FEATURE, s.ancestor_page, s.ancestor_id, s.leaf_page, s.leaf_id);
    if (this->flag_sensors_[i] != nullptr && this->flag_input_[i] == nullptr && this->flag_feature_[i] == nullptr) {
      ESP_LOGW(TAG, "flag %u not present in this UPS's descriptor", i);
    } else if (this->flag_sensors_[i] != nullptr) {
      bound++;
    }
  }

  // Collect the distinct feature reports that actually back a configured
  // entity, so polling costs one transfer per report rather than per field.
  this->feature_report_count_ = 0;
  auto remember = [this](const hid_pdc_field_t *f) {
    if (f == nullptr)
      return;
    for (size_t i = 0; i < this->feature_report_count_; i++)
      if (this->feature_report_ids_[i] == f->report_id)
        return;
    if (this->feature_report_count_ < MAX_FEATURE_REPORTS)
      this->feature_report_ids_[this->feature_report_count_++] = f->report_id;
  };
  for (uint8_t i = 0; i < METRIC_COUNT; i++)
    if (this->metric_sensors_[i] != nullptr)
      remember(this->metric_feature_[i]);
  // Flags are polled even when the interrupt endpoint also carries them: the
  // UPS only pushes on change, so without a poll a flag keeps its default
  // until something happens, which on a healthy UPS could be never.
  for (uint8_t i = 0; i < FLAG_COUNT; i++)
    if (this->flag_sensors_[i] != nullptr)
      remember(this->flag_feature_[i]);

  ESP_LOGI(TAG, "bound %u entities, polling %u feature reports every %us", bound, this->feature_report_count_,
           this->poll_interval_ / 1000);
}

void APCUPSClient::start_interrupt_in_() {
  const bool submitted = this->transfer_in(
      EP_INTERRUPT_IN,
      // CALLBACK CONTEXT: USB task.
      [this](const usb_host::TransferStatus &status) {
        if (status.success && status.data_len >= 2)
          this->decode_report_(HID_PDC_INPUT, status.data[0], status.data + 1, status.data_len - 1);
        // Re-arm regardless: a failed read must not end the subscription.
        this->start_interrupt_in_();
      },
      REPORT_READ_LEN);

  if (!submitted)
    ESP_LOGW(TAG, "could not submit interrupt read");
}

void APCUPSClient::poll_feature_reports_() {
  for (size_t i = 0; i < this->feature_report_count_; i++) {
    const uint8_t report_id = this->feature_report_ids_[i];
    const uint8_t type = usb_host::USB_DIR_IN | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE;
    const std::vector<uint8_t> read_buffer(REPORT_READ_LEN);

    this->control_transfer(
        type, REQ_GET_REPORT, REPORT_TYPE_FEATURE | report_id, HID_INTERFACE,
        // CALLBACK CONTEXT: USB task.
        [this, report_id](const usb_host::TransferStatus &status) {
          if (!status.success || status.data_len <= SETUP_PACKET_SIZE)
            return;
          const uint8_t *body = status.data + SETUP_PACKET_SIZE;
          size_t body_len = status.data_len - SETUP_PACKET_SIZE;
          // A device using report IDs prefixes the reply with the ID, but not
          // all do; only skip a byte that actually is the ID we asked for.
          if (body_len > 0 && body[0] == report_id) {
            body++;
            body_len--;
          }
          this->decode_report_(HID_PDC_FEATURE, report_id, body, body_len);
        },
        read_buffer);
  }
}

void APCUPSClient::decode_report_(uint8_t report_type, uint8_t report_id, const uint8_t *payload, size_t len) {
  for (uint8_t i = 0; i < METRIC_COUNT; i++) {
    sensor::Sensor *s = this->metric_sensors_[i];
    const hid_pdc_field_t *f = report_type == HID_PDC_INPUT ? this->metric_input_[i] : this->metric_feature_[i];
    if (s == nullptr || f == nullptr || f->report_id != report_id)
      continue;
    int32_t raw;
    if (!hid_pdc_extract(f, payload, len, &raw))
      continue;
    const float value = hid_pdc_scale(f, raw);
    s->publish_state(value);

    if (i == METRIC_LOAD)
      this->last_load_ = value;
    else if (i == METRIC_NOMINAL_POWER)
      this->last_nominal_power_ = value;
    if (i == METRIC_LOAD || i == METRIC_NOMINAL_POWER)
      this->publish_power_();
  }

  for (uint8_t i = 0; i < FLAG_COUNT; i++) {
    binary_sensor::BinarySensor *s = this->flag_sensors_[i];
    const hid_pdc_field_t *f = report_type == HID_PDC_INPUT ? this->flag_input_[i] : this->flag_feature_[i];
    if (s == nullptr || f == nullptr || f->report_id != report_id)
      continue;
    int32_t raw;
    if (!hid_pdc_extract(f, payload, len, &raw))
      continue;
    s->publish_state(raw != 0);
  }
}

void APCUPSClient::loop() {
  // Not USBClient::loop(): that disables the loop as soon as the bus is idle,
  // which would stop feature polling and the field dump. Combine its work
  // check with ours into one decision, as usb_host.h prescribes.
  bool did_work = this->process_usb_events_();

  if (this->descriptor_ready_ && !this->bound_) {
    this->descriptor_ready_ = false;
    this->bound_ = true;

    const size_t len = this->descriptor_len_;
    if (!hid_pdc_parse(this->descriptor_, len, &this->map_)) {
      ESP_LOGE(TAG, "report descriptor malformed after %d fields", this->map_.count);
      return;
    }

    ESP_LOGI(TAG, "descriptor %u bytes -> %d fields (power_page=%d report_ids=%d truncated=%d)", len, this->map_.count,
             this->map_.has_power_page, this->map_.uses_report_ids, this->map_.truncated);

    this->bind_fields_();
    this->start_interrupt_in_();
    this->poll_feature_reports_();
    this->last_poll_ = millis();
    this->log_index_ = 0;
    did_work = true;
  }

  if (this->log_index_ >= 0) {
    this->log_next_fields_();
    did_work = true;
  }

  if (this->bound_) {
    const uint32_t now = millis();
    if (now - this->last_poll_ >= this->poll_interval_) {
      this->last_poll_ = now;
      this->poll_feature_reports_();
    }
    // Stay awake while a UPS is attached: the poll timer has no other clock.
    did_work = true;
  }

  if (!did_work)
    this->disable_loop();
}

void APCUPSClient::log_next_fields_() {
  const int end = std::min(this->log_index_ + FIELDS_PER_LOOP, this->map_.count);

  for (; this->log_index_ < end; this->log_index_++) {
    const hid_pdc_field_t &f = this->map_.fields[this->log_index_];
    char path[128];
    hid_pdc_format_path(&f, path, sizeof(path));

    static const char *const TYPE_NAME[] = {"input", "output", "feature"};
    const char *type = f.report_type < 3 ? TYPE_NAME[f.report_type] : "?";

    ESP_LOGV(TAG, "[%3d] rpt=0x%02X %-7s bit=%3u+%-2u lmin=%d lmax=%d exp=%d  %s", this->log_index_, f.report_id, type,
             f.bit_offset, f.bit_size, f.logical_min, f.logical_max, f.unit_exponent, path);
  }

  if (this->log_index_ >= this->map_.count)
    this->log_index_ = -1;
}

// The UPS reports its own rating, so output power needs no calibration: HID
// expresses power in 1e-7 W and ConfigActivePower carries exp=7, giving watts.
void APCUPSClient::publish_power_() {
  sensor::Sensor *s = this->metric_sensors_[METRIC_POWER];
  if (s == nullptr || std::isnan(this->last_load_) || std::isnan(this->last_nominal_power_))
    return;
  s->publish_state(this->last_load_ * this->last_nominal_power_ / 100.0f);
}

void APCUPSClient::dump_config() {
  ESP_LOGCONFIG(TAG, "APC UPS:");
  USBClient::dump_config();
}

}  // namespace esphome::apc_ups

#endif
