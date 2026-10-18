// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome::m5pm1 {

// The M5PM1 is a microcontroller-based PMIC reached over I2C. On the StickS3 it
// gates the LCD rail, so the panel stays dark until its GPIO2 is driven high --
// M5GFX does this during board detection, which is easy to miss when porting.
class M5PM1 : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  // Just after the bus, so the rail is live before the display initialises.
  float get_setup_priority() const override { return setup_priority::BUS - 1.0f; }

  void set_lcd_power(bool enable) { this->lcd_power_ = enable; }
  void set_ok_sensor(binary_sensor::BinarySensor *s) { this->ok_sensor_ = s; }

 protected:
  bool write_bit_(uint8_t reg, uint8_t mask, bool value);
  bool configure_gpio_output_(uint8_t pin, bool level);

  void publish_ok_(bool ok);

  bool lcd_power_{true};
  binary_sensor::BinarySensor *ok_sensor_{nullptr};
};

}  // namespace esphome::m5pm1
