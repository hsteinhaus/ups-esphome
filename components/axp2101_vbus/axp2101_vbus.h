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
  // Reads the status register there and then. The boost interlock runs during
  // the switch's restore, before any polling component has had an update, so a
  // cached state would still be its default at exactly the moment it matters.
  bool vbus_present();
  void set_pmu_init(bool enable) { this->pmu_init_ = enable; }
  void update() override;
  void dump_config() override;

 protected:
  optional<uint16_t> read_adc_mv_(uint8_t reg);

  bool pmu_init_{true};
};

}  // namespace esphome::axp2101_vbus
