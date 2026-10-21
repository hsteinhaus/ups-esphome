// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include <atomic>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/usb_host/usb_host.h"
#include "esphome/core/component.h"

#include "owon_dge_protocol.h"

namespace esphome::owon_dge {

enum Text : uint8_t {
  TEXT_MODEL,
  TEXT_SERIAL,
  TEXT_FIRMWARE,
  TEXT_COUNT,
};

// Off, or which of the exclusive modulation types is on. Not a setting of its
// own: it is read from, and written to, the per-type STATe settings.
enum Modulation : uint8_t {
  MODULATION_OFF,
  MODULATION_AM,
  MODULATION_FM,
  MODULATION_FSK,
};

class OwonDGE;

class OwonDGENumber : public number::Number, public Parented<OwonDGE> {
 public:
  explicit OwonDGENumber(Setting setting) : setting_(setting) {}

 protected:
  void control(float value) override;
  Setting setting_;
};

// A TOKEN setting, or (with SETTING_COUNT) the modulation type.
class OwonDGESelect : public select::Select, public Parented<OwonDGE> {
 public:
  explicit OwonDGESelect(Setting setting) : setting_(setting) {}

 protected:
  void control(size_t index) override;
  Setting setting_;
};

// A BOOL setting, or the load: on is High-Z, off is 50 ohm.
class OwonDGESwitch : public switch_::Switch, public Parented<OwonDGE> {
 public:
  explicit OwonDGESwitch(Setting setting) : setting_(setting) {}

 protected:
  void write_state(bool state) override;
  Setting setting_;
};

// Reads and controls an Owon DGE-series function generator over the raw SCPI
// its 5345:1234 bulk interface carries: commands out on EP 0x01, `\n`-ended
// replies in on EP 0x81.
//
// Sets are never answered, so every set is followed by its query and only the
// read-back is published.
class OwonDGE : public usb_host::USBClient {
 public:
  OwonDGE(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_number(Setting s, OwonDGENumber *n) { this->numbers_[s] = n; }
  void set_select(Setting s, OwonDGESelect *sel) { this->selects_[s] = sel; }
  void set_modulation_select(OwonDGESelect *sel) { this->modulation_select_ = sel; }
  void set_switch(Setting s, OwonDGESwitch *sw) { this->switches_[s] = sw; }
  void set_text_sensor(Text t, text_sensor::TextSensor *s) { this->text_sensors_[t] = s; }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_status_interval(uint32_t ms) { this->status_interval_ = ms; }
  void set_settings_interval(uint32_t ms) { this->settings_interval_ = ms; }
  void set_reply_timeout(uint32_t ms) { this->reply_timeout_ = ms; }

  // Called from entities, in entity units.
  void request_setting(Setting s, float value);
  void request_modulation(Modulation m);

  // The IDF USB host ISR cannot run during a flash erase; see usb_uart.h.
  void suspend_usb();
  void resume_usb();

 protected:
  void on_connected() override;
  void disconnect() override;
  void start_input_();
  void send_line_(const char *line);
  void send_query_(uint8_t query);
  void send_setting_(Setting s);
  void on_line_(const char *line);
  void on_timeout_();
  void parse_identity_(const char *reply);
  void on_read_back_(Setting s, float value);
  void publish_setting_(Setting s);
  void publish_modulation_();
  void set_connected_(bool connected);
  void reset_state_();

  static constexpr uint8_t QUERY_NONE = 0xFF;
  static constexpr uint8_t QUERY_IDENTITY = 0xFE;
  static constexpr size_t MAX_LINE = 96;

  OwonDGENumber *numbers_[SETTING_COUNT]{};
  OwonDGESelect *selects_[SETTING_COUNT]{};
  OwonDGESwitch *switches_[SETTING_COUNT]{};
  OwonDGESelect *modulation_select_{nullptr};
  text_sensor::TextSensor *text_sensors_[TEXT_COUNT]{};
  binary_sensor::BinarySensor *connected_sensor_{nullptr};

  // Interface claimed and endpoints usable.
  bool claimed_{false};

  // One IN transfer at a time, completed on the USB task: it hands its bytes
  // over here, and the main loop takes them and re-arms.
  uint8_t rx_chunk_[64]{};
  std::atomic<uint16_t> rx_chunk_len_{0};
  std::atomic<bool> rx_ready_{false};
  std::atomic<bool> in_pending_{false};
  std::atomic<bool> suspended_{false};
  char line_[MAX_LINE]{};
  size_t line_len_{0};

  // What the generator last read back, and what is waiting to be written.
  // NAN means unknown / nothing pending.
  float actual_[SETTING_COUNT];
  float target_[SETTING_COUNT];
  bool sent_[SETTING_COUNT]{};
  uint32_t sent_at_[SETTING_COUNT]{};

  // One query outstanding at a time: replies carry nothing that says which
  // query they answer.
  uint8_t in_flight_{QUERY_NONE};
  uint32_t command_started_{0};
  uint8_t missed_replies_{0};
  bool connected_{false};
  bool resync_{true};
  uint32_t last_rx_{0};

  uint8_t status_next_{0xFF};
  uint8_t round_next_{0};
  uint32_t last_status_{0};
  uint32_t last_round_{0};

  uint32_t status_interval_{1000};
  uint32_t settings_interval_{5000};
  uint32_t reply_timeout_{500};
};

}  // namespace esphome::owon_dge

#endif
