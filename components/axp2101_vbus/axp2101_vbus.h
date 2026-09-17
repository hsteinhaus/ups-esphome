#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"

namespace esphome::axp2101_vbus {

// Reads the AXP2101 PMU's view of the VBUS node -- the same node the CoreS3
// drives when USB_OTG_EN is enabled. Answers "is there 5V on the USB-C port"
// without a device plugged in, which every other available test depends on.
class AXP2101Vbus : public binary_sensor::BinarySensor, public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

 protected:
  optional<uint16_t> read_adc_mv_(uint8_t reg);
};

}  // namespace esphome::axp2101_vbus
