// SPDX-License-Identifier: GPL-2.0-only
// Host-side tests for the Megatec/Q* reply parser.
//
// The parser was written from the protocol, not from the device, so these
// pin down the field order and -- more importantly -- that a truncated reply
// fails instead of parsing into plausible numbers. The Cypress bridge drops
// leading bytes when it desyncs, which is exactly how a half-read line
// reaches the parser in the field.
//
//   g++ -std=c++17 -Wall -Wextra -Werror -o work/qx_protocol_test
//       tools/qx_protocol_test.cpp components/qx_ups/qx_protocol.cpp
//   work/qx_protocol_test

#include "../components/qx_ups/qx_protocol.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace esphome::qx_ups;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    printf("  FAIL  %s\n", what);
    failures++;
  }
}

static void check_near(float got, float want, const char *what) {
  if (std::fabs(got - want) > 0.001f) {
    printf("  FAIL  %s: got %.3f, want %.3f\n", what, got, want);
    failures++;
  }
}

static void test_status_fields() {
  QxStatus s{};
  check(qx_parse_status("(226.0 000.0 225.5 014 50.0 13.60 25.0 00001001", &s), "normal reply parses");
  check_near(s.value[QX_INPUT_VOLTAGE], 226.0f, "input voltage");
  check_near(s.value[QX_INPUT_FAULT_VOLTAGE], 0.0f, "input fault voltage");
  check_near(s.value[QX_OUTPUT_VOLTAGE], 225.5f, "output voltage");
  check_near(s.value[QX_LOAD], 14.0f, "load");
  check_near(s.value[QX_INPUT_FREQUENCY], 50.0f, "input frequency");
  check_near(s.value[QX_BATTERY_VOLTAGE], 13.6f, "battery voltage");
  check_near(s.value[QX_TEMPERATURE], 25.0f, "temperature");

  check(!s.bit[QX_UTILITY_FAIL], "mains present");
  check(!s.bit[QX_BATTERY_LOW], "battery not low");
  check(s.bit[QX_STANDBY_TYPE], "standby type bit set");
  check(s.bit[QX_BEEPER_ON], "beeper on");
  check(!s.bit[QX_SHUTDOWN_ACTIVE], "no shutdown");
}

static void test_status_on_battery() {
  QxStatus s{};
  check(qx_parse_status("(000.0 226.0 230.0 035 49.9 12.40 26.0 10001001", &s), "on-battery reply parses");
  check(s.bit[QX_UTILITY_FAIL], "utility fail set");
  check_near(s.value[QX_INPUT_VOLTAGE], 0.0f, "input collapsed");
  // The fault voltage latches what the mains was when it failed, so it stays
  // high while the input reads zero. Swapping the two would look plausible.
  check_near(s.value[QX_INPUT_FAULT_VOLTAGE], 226.0f, "fault voltage latched");
}

static void test_status_rejects_damage() {
  QxStatus s{};
  check(!qx_parse_status("(226.0 000.0", &s), "truncated mid-field rejected");
  check(!qx_parse_status("(226.0 000.0 225.5 014 50.0 13.60 25.0", &s), "missing status bits rejected");
  check(!qx_parse_status("(226.0 000.0 225.5 014 50.0 13.60 25.0 0000100", &s), "seven bits rejected");
  check(!qx_parse_status("(226.0 000.0 225.5 014 50.0 13.60 25.0 0000100X", &s), "non-bit character rejected");
  // What a desynced bridge actually delivers: the head of the line is gone.
  check(!qx_parse_status("014 50.0 13.60 25.0 00001001", &s), "reply missing its marker rejected");
  check(!qx_parse_status("", &s), "empty reply rejected");
  check(!qx_parse_status(nullptr, &s), "null reply rejected");
}

static void test_ratings() {
  QxRatings r{};
  check(qx_parse_ratings("#230.0 004 12.00 50.0", &r), "ratings parse");
  check_near(r.voltage, 230.0f, "rated voltage");
  check_near(r.current, 4.0f, "rated current");
  check_near(r.battery_voltage, 12.0f, "rated battery voltage");
  check_near(r.frequency, 50.0f, "rated frequency");
  check(!qx_parse_ratings("#230.0 004", &r), "short ratings rejected");
  check(!qx_parse_ratings("(230.0 004 12.00 50.0", &r), "wrong marker rejected");
}

static void test_identity_padded() {
  QxIdentity id{};
  // Spec layout: 15, 10 and 10 characters, space padded.
  check(qx_parse_identity("#INNO TECH      UPS 1200  V1.00     ", &id), "padded identity parses");
  check(strcmp(id.manufacturer, "INNO TECH") == 0, "manufacturer keeps its internal space");
  check(strcmp(id.model, "UPS 1200") == 0, "model");
  check(strcmp(id.firmware, "V1.00") == 0, "firmware");
}

static void test_identity_full_width_field() {
  QxIdentity id{};
  // A manufacturer that exactly fills 15 characters leaves one space, which
  // is why this cannot be split on whitespace.
  check(qx_parse_identity("#ABCDEFGHIJKLMNO 0123456789 9876543210", &id), "full-width identity parses");
  check(strcmp(id.manufacturer, "ABCDEFGHIJKLMNO") == 0, "full-width manufacturer kept whole");
  check(strcmp(id.model, "0123456789") == 0, "full-width model");
}

static void test_identity_short() {
  QxIdentity id{};
  check(qx_parse_identity("#ACME  SMART 700  1.2", &id), "short identity parses");
  check(strcmp(id.manufacturer, "ACME") == 0, "short manufacturer");
  check(strcmp(id.model, "SMART 700") == 0, "short model keeps its space");
  check(strcmp(id.firmware, "1.2") == 0, "short firmware");
  check(!qx_parse_identity("ACME  SMART 700  1.2", &id), "identity without marker rejected");
}

static void test_battery_scaling() {
  // The real device: an ABB PowerValue 11 RT G2 reports 2.27 V for a 24 V
  // pack, which is one cell of twelve at float charge.
  QxBattery b = qx_scale_battery(2.27f, 24.0f);
  check(b.per_cell, "2.27 V against a 24 V rating is per cell");
  check_near(b.voltage, 27.24f, "scaled to the pack");
  check_near(b.nominal, 24.0f, "nominal stays the pack rating");

  b = qx_scale_battery(2.27f, 12.0f);
  check_near(b.voltage, 13.62f, "six cells for a 12 V pack");

  b = qx_scale_battery(13.6f, 12.0f);
  check(!b.per_cell, "13.6 V against a 12 V rating is the pack itself");
  check_near(b.voltage, 13.6f, "pack voltage passes through");

  // No `F` reply: the reading cannot be scaled, but charge must still work.
  b = qx_scale_battery(2.27f, NAN);
  check(b.per_cell, "a low reading is per cell even with no rating");
  check_near(b.voltage, 2.27f, "unscaled without a cell count");
  check_near(b.nominal, 2.0f, "charge falls back to one cell");

  b = qx_scale_battery(27.2f, NAN);
  check(!b.per_cell, "a pack-sized reading with no rating is a pack");
  check(std::isnan(b.nominal), "no nominal without a rating");
}

static void test_charge_estimate() {
  check_near(qx_charge_percent(13.6f, 12.0f), 100.0f, "float voltage reads full");
  check_near(qx_charge_percent(10.0f, 12.0f), 0.0f, "10 V on a 12 V pack reads empty");
  check_near(qx_charge_percent(27.24f, 24.0f), 100.0f, "the real pack reads full");
  check_near(qx_charge_percent(23.6f, 24.0f), 50.0f, "midpoint of a 24 V pack (20.0-27.2 V)");
  check_near(qx_charge_percent(5.0f, 12.0f), 0.0f, "below empty clamps, never negative");
  check_near(qx_charge_percent(99.0f, 12.0f), 100.0f, "above full clamps");
  // The bug this replaces: a per-cell reading against a pack nominal read 0%
  // on a perfectly healthy battery.
  check(std::isnan(qx_charge_percent(13.6f, NAN)), "no nominal yields no estimate");
}

int main() {
  test_status_fields();
  test_status_on_battery();
  test_status_rejects_damage();
  test_ratings();
  test_identity_padded();
  test_identity_full_width_field();
  test_identity_short();
  test_battery_scaling();
  test_charge_estimate();

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures != 0;
}
