# Vendored: usb_uart

ESPHome's own `usb_uart` from 2026.6.5 (`esphome/components/usb_uart/`),
shadowing the built-in through `external_components`. Committed unmodified
first, so `git log -p` on this directory shows the patch alone.

## The patch

`suspend_usb()` / `resume_usb()` on `USBUartComponent`, and a `suspended_`
check in `start_input()` and `start_output()` so neither re-arms while
suspended. The upstream component keeps a bulk IN transfer armed at all times
and offers no way to stop it, which an OTA flash erase cannot survive -- see
"The USB host ISR cannot survive a flash erase" in `docs/STATUS.md`.

## Upgrading ESPHome

Copy the new upstream files over these, commit that unmodified, then re-apply
the patch. A version skew between this copy and the `usb_host` it builds
against fails at compile time, not at runtime.
