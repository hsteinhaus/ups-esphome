// SPDX-License-Identifier: GPL-2.0-only
#include "owon_dge_protocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome::owon_dge {

// The firmware's spelling, which differs from the DGE2000 manual: PULS, not PUL.
static const char *const SHAPES[] = {"SIN", "SQU", "PULS", "RAMP", "PRN", "DC"};
// The manual's SUQ is ignored; SQU is what the unit takes and reports.
static const char *const MOD_SHAPES[] = {"SIN", "SQU", "RAMP", "PRN"};
static const char *const MOD_SOURCES[] = {"INT", "EXT"};
static const char *const SPACINGS[] = {"LIN", "LOG"};
static const char *const TRIGGER_SOURCES[] = {"TIM", "MAN", "EXT"};
static const char *const BURST_MODES[] = {"TRIG", "GAT"};
static const char *const POLARITIES[] = {"POS", "NEG"};

#define NUM(scpi) {scpi, Kind::NUMBER, nullptr, 0, 1.0f}
#define BOOL(scpi) {scpi, Kind::BOOL, nullptr, 0, 1.0f}
#define TOK(scpi, list) {scpi, Kind::TOKEN, list, sizeof(list) / sizeof(list[0]), 1.0f}

static const SettingDef DEFS[SETTING_COUNT] = {
    BOOL("OUTP1"),
    NUM("OUTP1:IMP"),
    TOK("SOUR1:FUNC:SHAP", SHAPES),
    NUM("SOUR1:FREQ"),
    NUM("SOUR1:VOLT"),
    NUM("SOUR1:VOLT:OFFS"),
    {"SOUR1:PHAS", Kind::NUMBER, nullptr, 0, static_cast<float>(M_PI / 180.0)},
    NUM("SOUR1:FUNC:RAMP:SYMM"),
    NUM("SOUR1:PULS:DCYC"),
    NUM("SOUR1:PULS:WIDT"),
    NUM("SOUR1:PULS:TRAN"),
    NUM("SOUR1:PULS:TRAN:TRA"),
    BOOL("SOUR1:AM:STAT"),
    NUM("SOUR1:AM:DEPT"),
    TOK("SOUR1:AM:SOUR", MOD_SOURCES),
    NUM("SOUR1:AM:INT:FREQ"),
    TOK("SOUR1:AM:INT:FUNC", MOD_SHAPES),
    BOOL("SOUR1:FM:STAT"),
    NUM("SOUR1:FM:DEV"),
    TOK("SOUR1:FM:SOUR", MOD_SOURCES),
    NUM("SOUR1:FM:INT:FREQ"),
    TOK("SOUR1:FM:INT:FUNC", MOD_SHAPES),
    BOOL("SOUR1:FSK:STAT"),
    NUM("SOUR1:FSK:FREQ"),
    NUM("SOUR1:FSK:INT:RATE"),
    TOK("SOUR1:FSK:SOUR", MOD_SOURCES),
    BOOL("SOUR1:SWE:STAT"),
    TOK("SOUR1:SWE:SPAC", SPACINGS),
    NUM("SOUR1:SWE:TIME"),
    TOK("SOUR1:SWE:SOUR", TRIGGER_SOURCES),
    NUM("SOUR1:FREQ:STAR"),
    NUM("SOUR1:FREQ:STOP"),
    BOOL("SOUR1:BURS:STAT"),
    TOK("SOUR1:BURS:MODE", BURST_MODES),
    NUM("SOUR1:BURS:NCYC"),
    NUM("SOUR1:BURS:INT:PER"),
    TOK("SOUR1:BURS:SOUR", TRIGGER_SOURCES),
    TOK("SOUR1:BURS:GATE:POL", POLARITIES),
};

#undef NUM
#undef BOOL
#undef TOK

// The firmware reports High-Z as 9.9E+37, SCPI's conventional infinity.
static constexpr float SCPI_INFINITY = 9.9e37f;

const SettingDef &setting_def(Setting s) { return DEFS[s]; }

static bool fits(int n, size_t len) { return n > 0 && static_cast<size_t>(n) < len; }

bool format_query(Setting s, char *buf, size_t len) {
  return fits(snprintf(buf, len, "%s?", DEFS[s].scpi), len);
}

bool format_set(Setting s, float value, char *buf, size_t len) {
  const SettingDef &d = DEFS[s];
  switch (d.kind) {
    case Kind::BOOL:
      return fits(snprintf(buf, len, "%s %s", d.scpi, value > 0.5f ? "ON" : "OFF"), len);
    case Kind::TOKEN: {
      const long i = std::lround(value);
      if (std::isnan(value) || i < 0 || i >= d.n_tokens)
        return false;
      return fits(snprintf(buf, len, "%s %s", d.scpi, d.tokens[i]), len);
    }
    case Kind::NUMBER:
    default:
      if (std::isinf(value))
        return fits(snprintf(buf, len, "%s INF", d.scpi), len);
      // %.9g keeps a 60 MHz frequency's sub-hertz digits.
      return fits(snprintf(buf, len, "%s %.9g", d.scpi, static_cast<double>(value * d.wire_per_unit)), len);
  }
}

float parse_reply(Setting s, const char *reply) {
  if (reply == nullptr || *reply == '\0')
    return NAN;
  const SettingDef &d = DEFS[s];
  if (d.kind == Kind::TOKEN) {
    for (uint8_t i = 0; i < d.n_tokens; i++) {
      if (strcmp(reply, d.tokens[i]) == 0)
        return i;
    }
    return NAN;
  }
  char *end;
  const float v = strtof(reply, &end);
  if (end == reply || *end != '\0' || !std::isfinite(v))
    return NAN;
  if (d.kind == Kind::BOOL)
    return v == 0.0f || v == 1.0f ? v : NAN;
  if (v >= SCPI_INFINITY)
    return INFINITY;
  return v / d.wire_per_unit;
}

bool is_identity_reply(const char *reply) { return reply != nullptr && strncmp(reply, "OWON,DGE", 8) == 0; }

}  // namespace esphome::owon_dge
