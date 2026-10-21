// SPDX-License-Identifier: GPL-2.0-only
// Host-side tests for the DGE1060 SCPI table and reply parser.
//
// The tokens and query forms below were read off the unit, not the manual
// (which says PUL and SUQ where the firmware says PULS and SQU), so these pin
// the firmware's spelling, and that a damaged reply fails instead of parsing.
//
//   g++ -std=c++17 -Wall -Wextra -Werror -o work/owon_dge_protocol_test
//       tools/owon_dge_protocol_test.cpp components/owon_dge/owon_dge_protocol.cpp
//   work/owon_dge_protocol_test

#include "../components/owon_dge/owon_dge_protocol.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace esphome::owon_dge;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    printf("  FAIL  %s\n", what);
    failures++;
  }
}

static void check_near(float got, float want, const char *what) {
  if (!(std::fabs(got - want) <= 1e-4f * std::fmax(1.0f, std::fabs(want)))) {
    printf("  FAIL  %s: got %g, want %g\n", what, got, want);
    failures++;
  }
}

static void check_str(bool ok, const char *got, const char *want, const char *what) {
  if (!ok || strcmp(got, want) != 0) {
    printf("  FAIL  %s: got '%s', want '%s'\n", what, ok ? got : "(failed)", want);
    failures++;
  }
}

static void test_numbers() {
  check_near(parse_reply(SETTING_FREQUENCY, "3.200000e+02"), 320.0f, "frequency NR3");
  check_near(parse_reply(SETTING_RAMP_SYMMETRY, "50.0"), 50.0f, "symmetry NR2");
  check_near(parse_reply(SETTING_AM_DEPTH, "100"), 100.0f, "AM depth NR1");
  // Phase travels in radians; entities are in degrees.
  check_near(parse_reply(SETTING_PHASE, "3.141593e+00"), 180.0f, "phase rad to deg");
  check(std::isinf(parse_reply(SETTING_LOAD, "9.9E+37")), "High-Z load reads as infinity");
  check_near(parse_reply(SETTING_LOAD, "5.000000e+01"), 50.0f, "50 ohm load");
  check_near(parse_reply(SETTING_PULSE_WIDTH, "2.247191e-04"), 224.7191f, "pulse width s to us");
}

static void test_tokens() {
  check_near(parse_reply(SETTING_SHAPE, "SIN"), 0.0f, "shape SIN");
  check_near(parse_reply(SETTING_SHAPE, "PULS"), 2.0f, "shape PULS");
  check_near(parse_reply(SETTING_SHAPE, "DC"), 5.0f, "shape DC");
  check(std::isnan(parse_reply(SETTING_SHAPE, "PUL")), "manual's PUL is not the firmware's");
  check_near(parse_reply(SETTING_FM_SHAPE, "SQU"), 1.0f, "mod shape SQU");
  check_near(parse_reply(SETTING_BURST_GATE_POLARITY, "POS"), 0.0f, "gate POS");
  check_near(parse_reply(SETTING_SWEEP_SOURCE, "TIM"), 0.0f, "sweep source TIM");
}

static void test_bools() {
  check_near(parse_reply(SETTING_OUTPUT, "1"), 1.0f, "output on");
  check_near(parse_reply(SETTING_OUTPUT, "0"), 0.0f, "output off");
  check_near(parse_reply(SETTING_BURST_STATE, "1"), 1.0f, "burst on");
  check(std::isnan(parse_reply(SETTING_OUTPUT, "x")), "non-bool rejected");
}

static void test_rejects_damage() {
  check(std::isnan(parse_reply(SETTING_FREQUENCY, "")), "empty rejected");
  check(std::isnan(parse_reply(SETTING_FREQUENCY, "3.2e")), "truncated exponent rejected");
  check(std::isnan(parse_reply(SETTING_FREQUENCY, "OWON,DGE1060,24500065,SCPI:99.0 FV:V1.0.4.4.0")),
        "identity in a number slot rejected");
  check(std::isnan(parse_reply(SETTING_SHAPE, "3.200000e+02")), "number in a token slot rejected");
  check(std::isnan(parse_reply(SETTING_FREQUENCY, nullptr)), "null rejected");
}

static void test_format() {
  char buf[48];
  check_str(format_query(SETTING_PULSE_TRAIL, buf, sizeof(buf)), buf, "SOUR1:PULS:TRAN:TRA?", "trail query");
  check_str(format_query(SETTING_PULSE_LEAD, buf, sizeof(buf)), buf, "SOUR1:PULS:TRAN?", "lead query");
  check_str(format_query(SETTING_OUTPUT, buf, sizeof(buf)), buf, "OUTP1?", "output query");
  check_str(format_query(SETTING_LOAD, buf, sizeof(buf)), buf, "OUTP1:IMP?", "load query");
  check_str(format_set(SETTING_SHAPE, 4, buf, sizeof(buf)), buf, "SOUR1:FUNC:SHAP PRN", "shape set");
  check_str(format_set(SETTING_OUTPUT, 1, buf, sizeof(buf)), buf, "OUTP1 ON", "output set");
  check_str(format_set(SETTING_SWEEP_STATE, 0, buf, sizeof(buf)), buf, "SOUR1:SWE:STAT OFF", "sweep off");
  check_str(format_set(SETTING_LOAD, INFINITY, buf, sizeof(buf)), buf, "OUTP1:IMP INF", "High-Z set");
  check(format_set(SETTING_PHASE, 90, buf, sizeof(buf)) && strncmp(buf, "SOUR1:PHAS 1.5707", 17) == 0, "phase deg to rad");
  check(format_set(SETTING_FREQUENCY, 12345.678f, buf, sizeof(buf)) &&
            std::fabs(strtof(buf + strlen("SOUR1:FREQ "), nullptr) - 12345.678f) < 0.01f,
        "frequency keeps its digits");
  check(format_set(SETTING_PULSE_TRAIL, 6.734f, buf, sizeof(buf)) &&
            std::fabs(strtof(buf + strlen("SOUR1:PULS:TRAN:TRA "), nullptr) - 6.734e-6f) < 1e-10f,
        "pulse edge us to s");
  check(!format_set(SETTING_SHAPE, 9, buf, sizeof(buf)), "out-of-range token index refused");
  check(!format_query(SETTING_FREQUENCY, buf, 4), "short buffer refused");
}

static void test_identity() {
  check(is_identity_reply("OWON,DGE1060,24500065,SCPI:99.0 FV:V1.0.4.4.0"), "IDN recognised");
  check(!is_identity_reply("3.200000e+02"), "number is not IDN");
  check(!is_identity_reply("OWON,SPE6103,24121050,FV:V5.1.0"), "PSU IDN is not a DGE");
}

int main() {
  test_numbers();
  test_tokens();
  test_bools();
  test_rejects_damage();
  test_format();
  test_identity();

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures != 0;
}
