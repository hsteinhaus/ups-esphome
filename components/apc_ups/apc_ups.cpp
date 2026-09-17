#include "apc_ups.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include "esphome/core/log.h"
#include <cstring>

namespace esphome::apc_ups {

static const char *const TAG = "apc_ups";

// GET_DESCRIPTOR(HID report) on the interface: standard IN request, descriptor
// type 0x22 in the high byte of wValue.
static constexpr uint8_t REQ_GET_DESCRIPTOR = 0x06;
static constexpr uint16_t DESC_TYPE_HID_REPORT = 0x2200;
static constexpr uint16_t HID_INTERFACE = 0;

// The BX950MI's descriptor is a few hundred bytes; ask for the buffer size and
// take the short read. usb_host raises CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE
// to 1024, so this fits in one transfer.
static constexpr uint16_t DESCRIPTOR_REQUEST_LEN = 1024 - 8;

// Logging the whole field table in one go overruns the log buffer, so it is
// drained a few lines per loop instead.
static constexpr int FIELDS_PER_LOOP = 4;

void APCUPSClient::on_connected() {
  ESP_LOGI(TAG, "UPS connected, requesting HID report descriptor");
  this->parsed_ = false;
  this->log_index_ = -1;
  this->descriptor_ready_ = false;
  this->descriptor_len_ = 0;
  this->request_report_descriptor_();
}

void APCUPSClient::on_disconnected() {
  ESP_LOGI(TAG, "UPS disconnected");
  this->parsed_ = false;
  this->log_index_ = -1;
  this->descriptor_ready_ = false;
  USBClient::on_disconnected();
}

void APCUPSClient::request_report_descriptor_() {
  const uint8_t type = usb_host::USB_DIR_IN | usb_host::USB_TYPE_STANDARD | usb_host::USB_RECIP_INTERFACE;
  const std::vector<uint8_t> read_buffer(DESCRIPTOR_REQUEST_LEN);

  // CALLBACK CONTEXT: USB task. Copy and hand over; do not parse here.
  const bool submitted = this->control_transfer(
      type, REQ_GET_DESCRIPTOR, DESC_TYPE_HID_REPORT, HID_INTERFACE,
      [this](const usb_host::TransferStatus &status) {
        if (!status.success || status.data_len == 0) {
          ESP_LOGE(TAG, "report descriptor request failed (err=%u, len=%u)", status.error_code, status.data_len);
          return;
        }
        const size_t len = std::min(static_cast<size_t>(status.data_len), MAX_DESCRIPTOR);
        memcpy(this->descriptor_, status.data, len);
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

void APCUPSClient::loop() {
  // Not USBClient::loop(): that disables the loop as soon as the bus is idle,
  // which would stop the field-table drain after its first batch. Combine its
  // work check with ours into one decision, as usb_host.h prescribes.
  bool did_work = this->process_usb_events_();

  if (this->descriptor_ready_ && !this->parsed_) {
    this->descriptor_ready_ = false;
    this->parsed_ = true;

    const size_t len = this->descriptor_len_;
    if (!hid_pdc_parse(this->descriptor_, len, &this->map_)) {
      ESP_LOGE(TAG, "report descriptor malformed after %d fields", this->map_.count);
      return;
    }

    ESP_LOGI(TAG, "descriptor %u bytes -> %d fields (power_page=%d report_ids=%d truncated=%d)", len, this->map_.count,
             this->map_.has_power_page, this->map_.uses_report_ids, this->map_.truncated);
    this->log_index_ = 0;
    did_work = true;
  }

  if (this->log_index_ >= 0) {
    this->log_next_fields_();
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

    ESP_LOGD(TAG, "[%3d] rpt=0x%02X %-7s bit=%3u+%-2u lmin=%d lmax=%d exp=%d  %s", this->log_index_, f.report_id, type,
             f.bit_offset, f.bit_size, f.logical_min, f.logical_max, f.unit_exponent, path);
  }

  if (this->log_index_ >= this->map_.count) {
    ESP_LOGI(TAG, "field table dumped");
    this->log_index_ = -1;
  }
}

void APCUPSClient::dump_config() {
  ESP_LOGCONFIG(TAG, "APC UPS:");
  USBClient::dump_config();
}

}  // namespace esphome::apc_ups

#endif
