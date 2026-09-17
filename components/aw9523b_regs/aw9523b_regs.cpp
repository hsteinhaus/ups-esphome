#include "aw9523b_regs.h"
#include "esphome/core/log.h"

namespace esphome::aw9523b_regs {

static const char *const TAG = "aw9523b_regs";

static constexpr uint8_t REG_INPUT_P0 = 0x00;
static constexpr uint8_t REG_CTL = 0x11;
static constexpr uint8_t REG_LED_MODE_P0 = 0x12;

static constexpr uint8_t USB_OTG_EN_BIT = 1 << 5;  // P0_5
static constexpr uint8_t BOOST_EN_BIT = 1 << 7;    // P1_7

void AW9523BRegs::update() {
  uint8_t io[6];  // 0x00..0x05: input P0/P1, output P0/P1, config P0/P1
  uint8_t ctl;
  uint8_t led_mode[2];

  if (!this->read_bytes(REG_INPUT_P0, io, sizeof(io)) || !this->read_byte(REG_CTL, &ctl) ||
      !this->read_bytes(REG_LED_MODE_P0, led_mode, sizeof(led_mode))) {
    this->status_set_warning("register read failed");
    return;
  }
  this->status_clear_warning();

  // Config bits are 1 for input, 0 for output. LED-mode bits are 1 for GPIO.
  ESP_LOGD(TAG, "in=%02X %02X  out=%02X %02X  cfg=%02X %02X  ctl=%02X  ledmode=%02X %02X", io[0], io[1], io[2], io[3],
           io[4], io[5], ctl, led_mode[0], led_mode[1]);
  ESP_LOGD(TAG, "USB_OTG_EN P0_5: output=%d is_output=%d gpio_mode=%d pin_reads=%d",
           (io[2] & USB_OTG_EN_BIT) != 0, (io[4] & USB_OTG_EN_BIT) == 0, (led_mode[0] & USB_OTG_EN_BIT) != 0,
           (io[0] & USB_OTG_EN_BIT) != 0);
  ESP_LOGD(TAG, "BOOST_EN   P1_7: output=%d is_output=%d gpio_mode=%d pin_reads=%d",
           (io[3] & BOOST_EN_BIT) != 0, (io[5] & BOOST_EN_BIT) == 0, (led_mode[1] & BOOST_EN_BIT) != 0,
           (io[1] & BOOST_EN_BIT) != 0);
}

void AW9523BRegs::dump_config() {
  ESP_LOGCONFIG(TAG, "AW9523B register dump:");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace esphome::aw9523b_regs
