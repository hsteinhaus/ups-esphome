// SPDX-License-Identifier: GPL-2.0-only
#include "qx_protocol.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace esphome::qx_ups {

// No real battery pack sits below this, so a reading under it is one cell.
static constexpr float MIN_PACK_VOLTAGE = 4.0f;
static constexpr float CELL_NOMINAL_VOLTAGE = 2.0f;
// Fractions of nominal at which a lead-acid pack is flat and fully charged:
// the 10.0 V and 13.6 V that a 12 V pack is conventionally measured against.
static constexpr float BATTERY_EMPTY_RATIO = 10.0f / 12.0f;
static constexpr float BATTERY_FULL_RATIO = 13.6f / 12.0f;

QxBattery qx_scale_battery(float reported, float rated_nominal) {
  if (reported >= MIN_PACK_VOLTAGE)
    return {reported, rated_nominal, false};

  // Per cell. The rating says how many cells to multiply by; without one the
  // reading stands as it is and charge is measured against a single cell.
  const float cells = std::isnan(rated_nominal) ? 0.0f : roundf(rated_nominal / CELL_NOMINAL_VOLTAGE);
  if (cells >= 1.0f)
    return {reported * cells, rated_nominal, true};
  return {reported, CELL_NOMINAL_VOLTAGE, true};
}

float qx_charge_percent(float voltage, float nominal) {
  if (std::isnan(nominal) || nominal <= 0.0f)
    return NAN;
  const float low = nominal * BATTERY_EMPTY_RATIO;
  const float high = nominal * BATTERY_FULL_RATIO;
  const float charge = (voltage - low) / (high - low) * 100.0f;
  return charge < 0.0f ? 0.0f : charge > 100.0f ? 100.0f : charge;
}

namespace {

// strtof that reports whether it actually consumed a number, so a missing
// field fails the whole reply instead of silently reading as zero.
bool take_float(const char **p, float *out) {
  char *end;
  const float value = strtof(*p, &end);
  if (end == *p)
    return false;
  *p = end;
  *out = value;
  return true;
}

void copy_trimmed(char *dst, size_t size, const char *src, size_t len) {
  while (len > 0 && src[len - 1] == ' ')
    len--;
  while (len > 0 && *src == ' ') {
    src++;
    len--;
  }
  if (len >= size)
    len = size - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

}  // namespace

bool qx_parse_status(const char *reply, QxStatus *out) {
  if (reply == nullptr || *reply != '(')
    return false;

  const char *p = reply + 1;
  for (unsigned char i = 0; i < QX_FIELD_COUNT; i++) {
    if (!take_float(&p, &out->value[i]))
      return false;
  }

  while (*p == ' ')
    p++;
  // Every bit must be present and must be a bit. The terminator is neither,
  // so a reply cut short fails here rather than reading past its end.
  for (unsigned char i = 0; i < QX_BIT_COUNT; i++) {
    if (p[i] != '0' && p[i] != '1')
      return false;
    out->bit[i] = p[i] == '1';
  }
  return true;
}

bool qx_parse_ratings(const char *reply, QxRatings *out) {
  if (reply == nullptr || *reply != '#')
    return false;

  const char *p = reply + 1;
  float value[4];
  for (float &field : value) {
    if (!take_float(&p, &field))
      return false;
  }
  out->voltage = value[0];
  out->current = value[1];
  out->battery_voltage = value[2];
  out->frequency = value[3];
  return true;
}

bool qx_parse_identity(const char *reply, QxIdentity *out) {
  if (reply == nullptr || *reply != '#')
    return false;

  const char *body = reply + 1;
  const size_t len = strlen(body);
  char *field[3] = {out->manufacturer, out->model, out->firmware};

  // The protocol pads the three fields to 15, 10 and 10 characters. A field
  // that exactly fills its width leaves a single space, so splitting on
  // whitespace would merge it into the next one -- use the widths whenever
  // the reply is long enough to carry them.
  static constexpr size_t WIDTH[3] = {15, 10, 10};
  static constexpr size_t PADDED_LEN = WIDTH[0] + 1 + WIDTH[1] + 1 + WIDTH[2];
  if (len >= PADDED_LEN) {
    size_t pos = 0;
    for (int i = 0; i < 3; i++) {
      copy_trimmed(field[i], QxIdentity::FIELD_SIZE, body + pos, WIDTH[i]);
      pos += WIDTH[i] + 1;
    }
    return true;
  }

  // Shorter than the spec allows: fall back to splitting on runs of spaces,
  // since a manufacturer name may contain single ones.
  size_t pos = 0;
  for (int i = 0; i < 3; i++) {
    while (pos < len && body[pos] == ' ')
      pos++;
    const char *gap = strstr(body + pos, "  ");
    const size_t end = gap == nullptr ? len : static_cast<size_t>(gap - body);
    copy_trimmed(field[i], QxIdentity::FIELD_SIZE, body + pos, end - pos);
    pos = end;
  }
  return out->manufacturer[0] != '\0';
}

}  // namespace esphome::qx_ups
