// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

// The DGE1060's SCPI dialect, with no dependency on ESPHome or the USB stack
// so it can be compiled and tested on a host. See tools/owon_dge_protocol_test.cpp.
//
// Every entry was seen answering on the unit; a query the firmware does not
// know is never answered, so an entry that is wrong here stalls the poll.

namespace esphome::owon_dge {

// Everything the generator can be asked to change, each with its own query.
enum Setting : uint8_t {
  SETTING_OUTPUT,
  SETTING_LOAD,
  SETTING_SHAPE,
  SETTING_FREQUENCY,
  SETTING_AMPLITUDE,
  SETTING_OFFSET,
  SETTING_PHASE,
  SETTING_RAMP_SYMMETRY,
  SETTING_PULSE_DUTY,
  SETTING_PULSE_WIDTH,
  SETTING_PULSE_LEAD,
  SETTING_PULSE_TRAIL,
  SETTING_AM_STATE,
  SETTING_AM_DEPTH,
  SETTING_AM_SOURCE,
  SETTING_AM_FREQUENCY,
  SETTING_AM_SHAPE,
  SETTING_FM_STATE,
  SETTING_FM_DEVIATION,
  SETTING_FM_SOURCE,
  SETTING_FM_FREQUENCY,
  SETTING_FM_SHAPE,
  SETTING_FSK_STATE,
  SETTING_FSK_FREQUENCY,
  SETTING_FSK_RATE,
  SETTING_FSK_SOURCE,
  SETTING_SWEEP_STATE,
  SETTING_SWEEP_SPACING,
  SETTING_SWEEP_TIME,
  SETTING_SWEEP_SOURCE,
  SETTING_SWEEP_START,
  SETTING_SWEEP_STOP,
  SETTING_BURST_STATE,
  SETTING_BURST_MODE,
  SETTING_BURST_CYCLES,
  SETTING_BURST_PERIOD,
  SETTING_BURST_SOURCE,
  SETTING_BURST_GATE_POLARITY,
  SETTING_COUNT,
};

enum class Kind : uint8_t {
  NUMBER,
  BOOL,
  // One of a fixed list; the value is its index.
  TOKEN,
};

struct SettingDef {
  const char *scpi;  // header without '?', e.g. "SOUR1:FREQ"
  Kind kind;
  const char *const *tokens;
  uint8_t n_tokens;
  // Wire value per entity unit: phase is set and read in radians but shown in degrees.
  float wire_per_unit;
};

const SettingDef &setting_def(Setting s);

// "SOUR1:FREQ?" into buf. False if it does not fit.
bool format_query(Setting s, char *buf, size_t len);
// A value in entity units (token index, 0/1, or number) into a set command.
// False if it does not fit or the token index is out of range.
bool format_set(Setting s, float value, char *buf, size_t len);
// A reply line (no line ending) into entity units; NAN if it is not a reply
// of the right shape for this setting. A High-Z load reads as INFINITY.
float parse_reply(Setting s, const char *reply);
bool is_identity_reply(const char *reply);

}  // namespace esphome::owon_dge
