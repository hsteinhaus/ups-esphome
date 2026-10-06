#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include <atomic>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/usb_host/usb_host.h"

namespace esphome::qx_ups {

// Scalar measurements, in the order a Q1 reply carries them.
enum Metric : uint8_t {
  METRIC_INPUT_VOLTAGE,
  METRIC_INPUT_FAULT_VOLTAGE,
  METRIC_OUTPUT_VOLTAGE,
  METRIC_LOAD,
  METRIC_INPUT_FREQUENCY,
  METRIC_BATTERY_VOLTAGE,
  METRIC_TEMPERATURE,
  // Derived: the Q1 reply carries neither of these.
  METRIC_POWER,
  METRIC_BATTERY_LEVEL,
  METRIC_COUNT,
};

// The eight status bits of a Q1 reply, in the order the device sends them,
// plus ONLINE, which is the inverse of the first and the one worth alarming on.
enum Flag : uint8_t {
  FLAG_ONLINE,
  FLAG_ON_BATTERY,
  FLAG_LOW_BATTERY,
  FLAG_BOOST_BUCK,
  FLAG_UPS_FAILED,
  FLAG_TEST_IN_PROGRESS,
  FLAG_SHUTDOWN_ACTIVE,
  FLAG_BEEPER_ON,
  FLAG_COUNT,
};

// Fields of the `I` reply.
enum Text : uint8_t {
  TEXT_MANUFACTURER,
  TEXT_MODEL,
  TEXT_FIRMWARE,
  TEXT_COUNT,
};

// What the component is currently waiting for a reply to.
enum Command : uint8_t {
  CMD_NONE,
  CMD_STATUS,
  CMD_RATINGS,
  CMD_IDENTITY,
};

// Reads a UPS that speaks the Megatec/Q* protocol behind a Cypress-style
// HID-to-serial bridge (0665:5161 and relatives).
//
// There is nothing to parse in the HID report descriptor here: it describes an
// opaque byte pipe, not a power device. Commands go out as 8-byte HID output
// reports via SET_REPORT, replies come back on the interrupt IN endpoint in
// 8-byte chunks and are assembled until a carriage return.
class QxUPSClient : public usb_host::USBClient {
 public:
  QxUPSClient(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}

  void loop() override;
  void dump_config() override;

  void set_metric_sensor(Metric m, sensor::Sensor *s) { this->metric_sensors_[m] = s; }
  void set_flag_sensor(Flag f, binary_sensor::BinarySensor *s) { this->flag_sensors_[f] = s; }
  void set_text_sensor(Text t, text_sensor::TextSensor *s) { this->text_sensors_[t] = s; }
  void set_status_interval(uint32_t ms) { this->status_interval_ = ms; }
  void set_reply_timeout(uint32_t ms) { this->reply_timeout_ = ms; }
  void set_nominal_power(float watts) { this->nominal_power_ = watts; }
  void set_battery_voltage_range(float low, float high) {
    this->battery_low_v_ = low;
    this->battery_high_v_ = high;
  }

 protected:
  void on_connected() override;
  void on_disconnected() override;

  bool discover_hid_interface_();
  void start_interrupt_in_();
  void send_command_(Command cmd);
  void send_next_chunk_();
  void on_reply_(const char *reply);
  bool parse_status_(const char *reply);
  void parse_ratings_(const char *reply);
  void parse_identity_(const char *reply);
  void publish_metric_(Metric m, float value);
  void publish_flag_(Flag f, bool state);

  sensor::Sensor *metric_sensors_[METRIC_COUNT]{};
  binary_sensor::BinarySensor *flag_sensors_[FLAG_COUNT]{};
  text_sensor::TextSensor *text_sensors_[TEXT_COUNT]{};

  // The interrupt endpoint, read from the configuration descriptor. A
  // hardcoded length that is not a multiple of wMaxPacketSize is rejected by
  // the host stack for every IN transfer, which silences the endpoint.
  uint8_t hid_interface_{0};
  uint8_t interrupt_ep_{0};
  uint16_t interrupt_mps_{0};
  bool interface_claimed_{false};
  // Claimed by exchange: the USB task re-arms from the transfer callback and
  // the main loop recovers a lost subscription, and both must not submit.
  std::atomic<bool> interrupt_pending_{false};

  // A reply is assembled from interrupt chunks by the USB task and handed to
  // the main loop whole, so parsing never runs in a transfer callback.
  //
  // Only the USB task ever writes rx_buf_/rx_len_. The main loop discards a
  // partial line by asking for it through rx_reset_ rather than zeroing the
  // length under a callback that may be mid-append.
  static constexpr size_t MAX_REPLY = 128;
  char rx_buf_[MAX_REPLY]{};
  size_t rx_len_{0};
  std::atomic<bool> reply_ready_{false};
  std::atomic<bool> rx_reset_{false};

  // One command outstanding at a time: the bridge has no way to tell two
  // replies apart, so overlapping them would mix their bytes.
  static constexpr size_t MAX_COMMAND = 16;
  uint8_t tx_buf_[MAX_COMMAND]{};
  size_t tx_len_{0};
  size_t tx_off_{0};
  std::atomic<Command> in_flight_{CMD_NONE};
  uint32_t command_started_{0};

  bool identity_pending_{true};
  bool ratings_pending_{true};
  bool dialect_logged_{false};

  float nominal_power_{NAN};
  float battery_low_v_{NAN};
  float battery_high_v_{NAN};
  // From the `F` reply, used to size the charge estimate when the YAML does
  // not pin the range.
  float rated_battery_v_{NAN};

  uint32_t status_interval_{1000};
  uint32_t reply_timeout_{1000};
  uint32_t last_status_{0};
};

}  // namespace esphome::qx_ups

#endif
