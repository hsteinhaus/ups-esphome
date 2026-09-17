#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/usb_host/usb_host.h"

extern "C" {
#include "hid_pdc.h"
}

namespace esphome::apc_ups {

// Scalar measurements. Ordering must match METRIC_SPECS in the .cpp.
enum Metric : uint8_t {
  METRIC_BATTERY_LEVEL,
  METRIC_RUNTIME,
  METRIC_INPUT_VOLTAGE,
  METRIC_BATTERY_VOLTAGE,
  METRIC_LOAD,
  METRIC_NOMINAL_POWER,
  // Derived from load and nominal power; has no usage path of its own.
  METRIC_POWER,
  METRIC_COUNT,
};

// PresentStatus bits. Ordering must match FLAG_SPECS in the .cpp.
enum Flag : uint8_t {
  FLAG_ONLINE,
  FLAG_CHARGING,
  FLAG_DISCHARGING,
  FLAG_LOW_BATTERY,
  FLAG_REPLACE_BATTERY,
  FLAG_OVERLOAD,
  FLAG_SHUTDOWN_IMMINENT,
  FLAG_BATTERY_PRESENT,
  FLAG_COUNT,
};

// Reads an APC UPS over USB HID. Report layout comes from the descriptor the
// device provides at enumeration and is resolved by usage path, never by fixed
// offsets -- see VENDOR.md and docs/bx950mi-hid-map.md.
class APCUPSClient : public usb_host::USBClient {
 public:
  APCUPSClient(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}

  void loop() override;
  void dump_config() override;

  void set_metric_sensor(Metric m, sensor::Sensor *s) { this->metric_sensors_[m] = s; }
  void set_flag_sensor(Flag f, binary_sensor::BinarySensor *s) { this->flag_sensors_[f] = s; }
  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }

 protected:
  void on_connected() override;
  void on_disconnected() override;

  void request_report_descriptor_();
  void bind_fields_();
  void start_interrupt_in_();
  void poll_feature_reports_();
  void decode_report_(uint8_t report_type, uint8_t report_id, const uint8_t *payload, size_t len);
  void log_next_fields_();
  void publish_power_();

  // Filled by the USB task, consumed by the main loop: parsing a few hundred
  // fields does not belong in a transfer callback.
  static constexpr size_t MAX_DESCRIPTOR = 1024;
  uint8_t descriptor_[MAX_DESCRIPTOR];
  volatile size_t descriptor_len_{0};
  volatile bool descriptor_ready_{false};

  hid_pdc_map_t map_{};
  bool bound_{false};
  int log_index_{-1};

  sensor::Sensor *metric_sensors_[METRIC_COUNT]{};
  binary_sensor::BinarySensor *flag_sensors_[FLAG_COUNT]{};

  // Resolved per report type, so an interrupt report and a polled feature
  // report can carry the same value at different offsets.
  const hid_pdc_field_t *metric_input_[METRIC_COUNT]{};
  const hid_pdc_field_t *metric_feature_[METRIC_COUNT]{};
  const hid_pdc_field_t *flag_input_[FLAG_COUNT]{};
  const hid_pdc_field_t *flag_feature_[FLAG_COUNT]{};

  static constexpr size_t MAX_FEATURE_REPORTS = 12;
  uint8_t feature_report_ids_[MAX_FEATURE_REPORTS]{};
  size_t feature_report_count_{0};

  float last_load_{NAN};
  float last_nominal_power_{NAN};

  uint32_t poll_interval_{10000};
  uint32_t last_poll_{0};
  bool interface_claimed_{false};
};

}  // namespace esphome::apc_ups

#endif
