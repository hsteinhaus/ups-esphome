// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome::aw9523b_regs {

// Dumps the AW9523B's own registers. The driver logs what it intends to write;
// this reads back what the chip actually holds -- direction, GPIO-vs-LED mode,
// and the input registers, which reflect real pin voltage rather than intent.
class AW9523BRegs : public PollingComponent, public i2c::I2CDevice {
 public:
  void update() override;
  void dump_config() override;
};

}  // namespace esphome::aw9523b_regs
