// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/usb_uart/usb_uart.h"
#include "esphome/core/component.h"

namespace esphome::owon_psu {

// Fields of a MEAS:ALL:INFO? reply that are measurements.
enum Metric : uint8_t {
  METRIC_VOLTAGE,
  METRIC_CURRENT,
  METRIC_POWER,
  METRIC_COUNT,
};

// The three ON/OFF fields of MEAS:ALL:INFO?, plus whether the PSU answers at
// all. The order OVP, OCP, OTP is inferred, not seen: open clamps can trip
// none of them.
enum Flag : uint8_t {
  FLAG_OVP,
  FLAG_OCP,
  FLAG_OTP,
  FLAG_CONNECTED,
  FLAG_COUNT,
};

enum Text : uint8_t {
  TEXT_MODEL,
  TEXT_SERIAL,
  TEXT_FIRMWARE,
  TEXT_MODE,
  TEXT_COUNT,
};

// Everything the PSU can be asked to change. Each is set, then read back:
// the firmware ignores an out-of-range set without a reply or an error.
enum Setting : uint8_t {
  SETTING_VOLTAGE,
  SETTING_CURRENT,
  SETTING_VOLTAGE_LIMIT,
  SETTING_CURRENT_LIMIT,
  SETTING_OUTPUT,
  SETTING_COUNT,
};

// What the component is currently waiting for a reply to.
enum Command : uint8_t {
  CMD_NONE,
  CMD_IDENTITY,
  CMD_INFO,
  // One per Setting, in the same order, so a read-back maps back by offset.
  CMD_READ_VOLTAGE,
  CMD_READ_CURRENT,
  CMD_READ_VOLTAGE_LIMIT,
  CMD_READ_CURRENT_LIMIT,
  CMD_READ_OUTPUT,
};

class OwonPSU;

class OwonPSUNumber : public number::Number, public Parented<OwonPSU> {
 public:
  explicit OwonPSUNumber(Setting setting) : setting_(setting) {}

 protected:
  void control(float value) override;
  Setting setting_;
};

class OwonPSUSwitch : public switch_::Switch, public Parented<OwonPSU> {
 protected:
  void write_state(bool state) override;
};

// Reads and controls an Owon SPE-series lab PSU over the SCPI dialect its
// CH340 bridge carries (115200 8N1, `\n` out, `\r\n` back).
//
// Sets are never answered, so every set is followed by the matching query and
// only the read-back is published: an entity shows what the PSU holds, not
// what was asked for.
class OwonPSU : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_usb_channel(usb_uart::USBUartChannel *channel) {
    this->usb_channel_ = channel;
    this->set_uart_parent(channel);
  }
  void set_metric_sensor(Metric m, sensor::Sensor *s) { this->metric_sensors_[m] = s; }
  void set_flag_sensor(Flag f, binary_sensor::BinarySensor *s) { this->flag_sensors_[f] = s; }
  void set_text_sensor(Text t, text_sensor::TextSensor *s) { this->text_sensors_[t] = s; }
  void set_number(Setting s, OwonPSUNumber *n) { this->numbers_[s] = n; }
  void set_output_switch(OwonPSUSwitch *s) { this->output_switch_ = s; }
  void set_voltage_cap(number::Number *n) { this->voltage_cap_ = n; }
  void set_current_cap(number::Number *n) { this->current_cap_ = n; }
  void set_status_interval(uint32_t ms) { this->status_interval_ = ms; }
  void set_settings_interval(uint32_t ms) { this->settings_interval_ = ms; }
  void set_reply_timeout(uint32_t ms) { this->reply_timeout_ = ms; }
  void set_link_loss_timeout(uint32_t ms) { this->link_loss_timeout_ = ms; }

  // Called from entities. Rejected here, before it reaches the wire, if a
  // firmware cap forbids it.
  void request_setting(Setting s, float value);

  // The IDF USB host ISR cannot run during a flash erase; see usb_uart.h.
  // Polling stops too, so nothing queues up behind the halted pipe.
  void suspend_usb();
  void resume_usb();

 protected:
  void send_query_(Command cmd);
  void send_setting_(Setting s);
  void on_reply_(Command answered, const char *reply);
  void on_timeout_(Command missed);
  void parse_identity_(const char *reply);
  void parse_info_(const char *reply);
  void on_read_back_(Setting s, float value);
  void check_link_();
  void set_connected_(bool connected);
  void publish_metric_(Metric m, float value);
  void publish_flag_(Flag f, bool state);
  void publish_text_(Text t, const std::string &value);

  usb_uart::USBUartChannel *usb_channel_{nullptr};
  sensor::Sensor *metric_sensors_[METRIC_COUNT]{};
  binary_sensor::BinarySensor *flag_sensors_[FLAG_COUNT]{};
  text_sensor::TextSensor *text_sensors_[TEXT_COUNT]{};
  OwonPSUNumber *numbers_[SETTING_COUNT]{};
  OwonPSUSwitch *output_switch_{nullptr};
  number::Number *voltage_cap_{nullptr};
  number::Number *current_cap_{nullptr};

  // What the PSU last read back, and what is waiting to be written. NAN means
  // unknown / nothing pending; the output uses 0 and 1.
  float actual_[SETTING_COUNT];
  float target_[SETTING_COUNT];
  // Sent and awaiting its read-back. Bulk USB does not lose bytes, so a set
  // goes out once; only the query after it is repeated.
  bool sent_[SETTING_COUNT]{};

  static constexpr size_t MAX_REPLY = 96;
  char rx_buf_[MAX_REPLY]{};
  size_t rx_len_{0};

  // One query outstanding at a time: replies carry nothing that says which
  // query they answer.
  Command in_flight_{CMD_NONE};
  uint32_t command_started_{0};
  uint8_t missed_replies_{0};
  bool connected_{false};
  // Set at start, after any timeout and across a USB gap: see loop().
  bool resync_{true};
  std::string serial_;
  bool suspended_{false};
  uint8_t next_read_{0};

  uint32_t status_interval_{500};
  uint32_t settings_interval_{5000};
  uint32_t reply_timeout_{500};
  uint32_t last_status_{0};
  uint32_t last_settings_{0};

  // Output off on link loss: armed by the API going from connected to not,
  // fired once when the gap outlasts the timeout. A boot without a client is
  // not a loss, so the PSU is never switched by a reboot.
  uint32_t link_loss_timeout_{10000};
  bool api_was_connected_{false};
  uint32_t api_lost_at_{0};
  bool link_trip_armed_{false};
};

}  // namespace esphome::owon_psu

#endif
