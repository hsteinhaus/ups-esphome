#pragma once

#include <cstddef>

// Megatec/Q* reply parsing, with no dependency on ESPHome or the USB stack so
// it can be compiled and tested on a host: this is the half of the component
// that was written without access to the device, and it is the half where a
// wrong field order produces plausible numbers rather than an error.
//
// Field and bit orders below are the protocol's, and are the contract the
// tests pin down. See tools/qx_protocol_test.cpp.

namespace esphome::qx_ups {

// Order of the numeric fields in a Q1 reply.
enum QxField : unsigned char {
  QX_INPUT_VOLTAGE,
  QX_INPUT_FAULT_VOLTAGE,
  QX_OUTPUT_VOLTAGE,
  QX_LOAD,
  QX_INPUT_FREQUENCY,
  QX_BATTERY_VOLTAGE,
  QX_TEMPERATURE,
  QX_FIELD_COUNT,
};

// Order of the eight status bits that close a Q1 reply.
enum QxStatusBit : unsigned char {
  QX_UTILITY_FAIL,
  QX_BATTERY_LOW,
  QX_BOOST_BUCK,
  QX_UPS_FAILED,
  // Nameplate, not state: 1 means a standby UPS, 0 a line-interactive or
  // online one. It never changes, so nothing publishes it.
  QX_STANDBY_TYPE,
  QX_TEST_IN_PROGRESS,
  QX_SHUTDOWN_ACTIVE,
  QX_BEEPER_ON,
  QX_BIT_COUNT,
};

struct QxStatus {
  float value[QX_FIELD_COUNT];
  bool bit[QX_BIT_COUNT];
};

struct QxRatings {
  float voltage;
  float current;
  float battery_voltage;
  float frequency;
};

struct QxIdentity {
  static constexpr size_t FIELD_SIZE = 24;
  char manufacturer[FIELD_SIZE];
  char model[FIELD_SIZE];
  char firmware[FIELD_SIZE];
};

// What the UPS reports as its battery voltage, resolved into a pack voltage.
struct QxBattery {
  float voltage;  // scaled to the whole pack
  float nominal;  // what a charge estimate should measure against; may be NAN
  bool per_cell;  // true when the device reported a single cell
};

// A Megatec UPS reports either the whole pack's voltage or a single cell's,
// and nothing in the protocol says which. `rated_nominal` is the `F` reply's
// battery rating, or NAN when the UPS never gave one.
QxBattery qx_scale_battery(float reported, float rated_nominal);

// Charge estimated from voltage, since the protocol carries none. NAN when
// there is no nominal voltage to measure against.
float qx_charge_percent(float voltage, float nominal);

// Each returns false for a reply that is truncated, mistyped or missing its
// leading marker. A partial reply must never parse: the bridge drops bytes
// when it desyncs, and a half-read line would otherwise read as real data.
bool qx_parse_status(const char *reply, QxStatus *out);
bool qx_parse_ratings(const char *reply, QxRatings *out);
bool qx_parse_identity(const char *reply, QxIdentity *out);

}  // namespace esphome::qx_ups
