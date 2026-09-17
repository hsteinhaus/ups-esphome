#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)

#include "esphome/components/usb_host/usb_host.h"

extern "C" {
#include "hid_pdc.h"
}

namespace esphome::apc_ups {

// Reads an APC UPS over USB HID. The report layout is taken from the
// descriptor the device provides at enumeration, never from fixed offsets --
// see VENDOR.md.
class APCUPSClient : public usb_host::USBClient {
 public:
  APCUPSClient(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}

  void loop() override;
  void dump_config() override;

 protected:
  void on_connected() override;
  void on_disconnected() override;

  void request_report_descriptor_();
  void log_next_fields_();

  // Filled by the USB task, consumed by the main loop: parsing and logging a
  // few hundred fields does not belong in a transfer callback.
  static constexpr size_t MAX_DESCRIPTOR = 1024;
  uint8_t descriptor_[MAX_DESCRIPTOR];
  volatile size_t descriptor_len_{0};
  volatile bool descriptor_ready_{false};

  hid_pdc_map_t map_{};
  bool parsed_{false};
  int log_index_{-1};
};

}  // namespace esphome::apc_ups

#endif
