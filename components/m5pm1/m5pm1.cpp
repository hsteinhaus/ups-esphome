#include "m5pm1.h"
#include "esphome/core/log.h"

namespace esphome::m5pm1 {

static const char *const TAG = "m5pm1";

// Register map as used by M5GFX's StickS3 detection path.
static constexpr uint8_t REG_GPIO_MODE = 0x10;      // 0 = input, 1 = output
static constexpr uint8_t REG_GPIO_LEVEL = 0x11;     // output level
static constexpr uint8_t REG_GPIO_DRIVE = 0x13;     // 0 = push-pull
static constexpr uint8_t REG_GPIO_FUNCTION = 0x16;  // 0 = plain GPIO
static constexpr uint8_t REG_I2C_CFG = 0x09;

// The PMIC keeps its own power, so a reset does not clear this: idle sleep left
// enabled by earlier firmware makes the device stop answering.
static constexpr uint8_t I2C_CFG_NO_IDLE_SLEEP = 0x00;

// GPIO2 drives the LCD's L3B enable.
static constexpr uint8_t PIN_LCD_POWER = 2;

bool M5PM1::write_bit_(uint8_t reg, uint8_t mask, bool value) {
  uint8_t current;
  if (!this->read_byte(reg, &current))
    return false;
  const uint8_t updated = value ? (current | mask) : (current & ~mask);
  return updated == current || this->write_byte(reg, updated);
}

bool M5PM1::configure_gpio_output_(uint8_t pin, bool level) {
  const uint8_t mask = 1 << pin;
  return this->write_bit_(REG_GPIO_FUNCTION, mask, false) && this->write_bit_(REG_GPIO_MODE, mask, true) &&
         this->write_bit_(REG_GPIO_DRIVE, mask, false) && this->write_bit_(REG_GPIO_LEVEL, mask, level);
}

void M5PM1::setup() {
  if (!this->write_byte(REG_I2C_CFG, I2C_CFG_NO_IDLE_SLEEP)) {
    ESP_LOGE(TAG, "PMIC did not answer");
    this->mark_failed();
    return;
  }

  if (this->lcd_power_ && !this->configure_gpio_output_(PIN_LCD_POWER, true)) {
    ESP_LOGE(TAG, "could not enable the LCD rail");
    this->mark_failed();
  }
}

void M5PM1::dump_config() {
  ESP_LOGCONFIG(TAG, "M5PM1 PMIC:\n  LCD rail: %s", YESNO(this->lcd_power_));
  LOG_I2C_DEVICE(this);
}

}  // namespace esphome::m5pm1
