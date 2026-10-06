// SPDX-License-Identifier: GPL-2.0-only
#include "axp2101_vbus.h"
#include "esphome/core/log.h"

namespace esphome::axp2101_vbus {

static const char *const TAG = "axp2101_vbus";

static constexpr uint8_t REG_STATUS1 = 0x00;
static constexpr uint8_t REG_ADC_CH_EN = 0x30;
static constexpr uint8_t REG_ADC_VBAT = 0x34;
static constexpr uint8_t REG_ADC_VBUS = 0x38;
static constexpr uint8_t REG_ADC_VSYS = 0x3A;

static constexpr uint8_t STATUS1_VBUS_GOOD = 1 << 5;
static constexpr uint8_t STATUS1_BATTERY_PRESENT = 1 << 3;

// VBAT | TS | VBUS | VSYS, matching what M5Unified enables on CoreS3.
static constexpr uint8_t ADC_ENABLE_ALL = 0x0F;

// 14-bit results, 1mV per LSB.
static constexpr uint8_t ADC_HIGH_BYTE_MASK = 0x3F;

// M5Unified's CoreS3 PMU init, which the ESPHome axp2101 component does not
// perform. Reproduced verbatim because it is the only configuration this board
// is known to boot USB host mode from; ADC enable (0x30) is part of it.
static const uint8_t CORE_S3_PMU_INIT[][2] = {
    {0x90, 0xBF},          // LDOS on/off control
    {0x92, 18 - 5},        // ALDO1 1.8V
    {0x93, 33 - 5},        // ALDO2 3.3V
    {0x94, 33 - 5},        // ALDO3 3.3V
    {0x95, 33 - 5},        // ALDO4 3.3V
    {0x27, 0x00},          // power key hold 1s / off 4s
    {0x69, 0x11},          // CHGLED
    {0x10, 0x30},          // PMU common config
    {REG_ADC_CH_EN, ADC_ENABLE_ALL},
};

void AXP2101Vbus::setup() {
  // Skipped where the axp2101 component already owns the PMU: two components
  // writing the same registers is a race, and the status bit needs no setup.
  if (!this->pmu_init_)
    return;

  for (const auto &entry : CORE_S3_PMU_INIT) {
    if (!this->write_byte(entry[0], entry[1])) {
      ESP_LOGE(TAG, "PMU init write to 0x%02X failed", entry[0]);
      this->mark_failed();
      return;
    }
  }
}

bool AXP2101Vbus::vbus_present() {
  uint8_t status;
  if (!this->read_byte(REG_STATUS1, &status)) {
    // Treated as present: the interlock exists to stop the boost fighting an
    // external supply, and a blind read must not be the reason it goes ahead.
    ESP_LOGW(TAG, "status read failed, assuming VBUS present");
    return true;
  }
  return (status & STATUS1_VBUS_GOOD) != 0;
}

optional<uint16_t> AXP2101Vbus::read_adc_mv_(uint8_t reg) {
  uint8_t raw[2];
  if (!this->read_bytes(reg, raw, sizeof(raw)))
    return {};
  return static_cast<uint16_t>(((raw[0] & ADC_HIGH_BYTE_MASK) << 8) | raw[1]);
}

void AXP2101Vbus::update() {
  uint8_t status;
  if (!this->read_byte(REG_STATUS1, &status)) {
    this->status_set_warning("reading status register failed");
    return;
  }
  this->status_clear_warning();

  const auto vbus = this->read_adc_mv_(REG_ADC_VBUS);
  const auto vbat = this->read_adc_mv_(REG_ADC_VBAT);
  const auto vsys = this->read_adc_mv_(REG_ADC_VSYS);

  // The whole register is logged: the individual bits are what we are debugging,
  // so a published boolean alone would hide a surprise in the other flags.
  ESP_LOGD(TAG, "status1=0x%02X vbus_good=%d battery_present=%d | vbus=%umV vbat=%umV vsys=%umV", status,
           (status & STATUS1_VBUS_GOOD) != 0, (status & STATUS1_BATTERY_PRESENT) != 0, vbus.value_or(0),
           vbat.value_or(0), vsys.value_or(0));

  this->publish_state((status & STATUS1_VBUS_GOOD) != 0);
}

void AXP2101Vbus::dump_config() {
  LOG_BINARY_SENSOR("", "AXP2101 VBUS", this);
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace esphome::axp2101_vbus
