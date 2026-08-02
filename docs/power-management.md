# Power Management

**Status: research started, nothing implemented yet.** First pass at what
"power states" actually means for this specific device, before writing any
code — same sequencing this project used before building the Weather API
client or the environmental APIs (see
[environmental-apis.md](environmental-apis.md)).

## The framing that changes everything here

This is a **wall-mounted, mains-powered kiosk** — every mockup, every
piece of firmware architecture, and `hardware.md`'s own notes assume the
device is always plugged in and always displaying something. `hardware.md`
already lists the Tab5's removable 2000 mAh battery under "Other onboard
peripherals **not used by this project**." That framing means most of
what "power management" usually means for an ESP32 project — deep sleep
to stretch battery life, wake-on-touch, minimizing average current draw —
**doesn't apply here** the way it would for a battery-powered sensor node.
Worth stating plainly rather than silently chasing sleep-mode research
that doesn't end up mattering.

What's actually relevant to an always-on mains-powered kiosk instead:

1. **Backlight control** — a real power/heat/panel-longevity lever even
   without battery constraints, and the one piece of "power management"
   this project's own mockups already gesture at (the day/night theme
   toggle) without actually implementing.
2. **Graceful power-loss/brownout behavior** — mains power can still cut
   out (someone unplugs it, a breaker trips), and this project has
   *already* hit real, empirically-observed brownout behavior worth
   collecting here rather than leaving scattered across commit messages.
3. **ESP32-P4 idle power modes** — even mains-powered, running the CPU at
   full tilt 24/7 for a screen that mostly displays static content is
   wasteful and generates needless heat. Worth knowing what's available,
   even if not deep sleep specifically.

## Backlight control

M5Unified's `M5.Display` (already this project's own display API —
`src/display.cpp` uses `M5.Display.startWrite()`/`pushImageDMA()`/
`setRotation()` today) exposes `M5.Display.setBrightness(0-255)`,
PWM-driven at 44.1 kHz. No brightness-control code exists in this project
yet. An auto-dim-at-night feature (distinct from the mockups' day/night
*color theme*, which is a palette swap, not a backlight change) would be
a straightforward addition once the local-time-of-day is known — which
`dashboard_ui.cpp`'s own clock code already resolves via `utcOffsetSec`.

## Brownout / power-loss behavior — already observed, not theoretical

This project has already hit real brownout behavior on hardware, captured
in commit history but not previously written up as a reference doc:

- A **software reset, panic reboot, or brownout-triggered restart** all
  leave RTC-retained memory intact — `settimeofday()` (called internally
  once NTP sync completes) persists the synced clock there as a side
  effect, the same mechanism deep-sleep wake relies on for time
  continuity. `main.cpp`'s `rtcTimeLooksValid()` checks this at the very
  start of `setup()`, before WiFi/provisioning even starts, and skips a
  redundant NTP round-trip if the clock already looks sane.
- **Correction from later hardware testing:** an actual **power-on
  reset** (mains power genuinely cut, not just a software/watchdog reset)
  *does* wipe RTC-retained memory on this chip — contradicting an earlier
  assumption in the same code's comments that brownout resets don't
  touch the RTC domain. They evidently do, at least as observed across
  this project's own brownout-heavy test cycles. `rtcTimeLooksValid()`'s
  behavior didn't need to change once this was corrected — it already
  falls back to a real NTP resync whenever RTC memory doesn't look
  valid — but it re-syncs more often across power cuts than originally
  expected.
- **Not yet investigated:** NVS (`ConfigStore`) integrity across a
  genuine mid-write power loss. ESP-IDF's NVS is designed to survive
  torn writes (it's log-structured, not a raw sector overwrite), but this
  project hasn't deliberately tested "yank power while `ConfigStore`
  is mid-save" the way it *has* tested reset/reboot/brownout paths.

## ESP32-P4 sleep modes (Light/Deep) — how they apply to *this* board

The P4 itself supports the standard ESP-IDF Light-sleep/Deep-sleep model
(CPU+RAM+peripherals powered down to varying degrees). Critically, unlike
a classic single-chip ESP32, **the P4 has no integrated radio at all** —
this board's WiFi/BT lives entirely on the separate ESP32-C6 co-processor,
bridged over SDIO (see `hardware.md`'s esp-hosted section). That
architectural split means:

- "Modem-sleep" (the classic ESP32 mode that keeps the CPU running but
  powers down just the radio) doesn't map cleanly onto this board — the
  radio isn't on the same chip to begin with.
- Whether/how the C6 co-processor's own power state can be managed
  independently of the P4's, and what happens to an in-flight SDIO
  session if the P4 enters light sleep, isn't documented anywhere found
  in this research pass. Given this project's already-extensive,
  hard-won esp-hosted quirks (SDIO crashes, DMA allocation failures,
  broken WiFi scanning — all in `hardware.md`), **this is exactly the
  kind of interaction that would need real on-hardware testing before
  ever being relied on**, not assumed to work the way single-chip ESP32
  sleep docs describe.
- Given point 1 in "the framing that changes everything" above, none of
  this may end up mattering — a kiosk that's supposed to always be
  showing the current dashboard has little reason to sleep at all. Noted
  for completeness, not because a use case has been identified yet.

## M5Unified `Power` class — available, mostly not applicable here

For completeness, the full API surface (`M5.Power`):
`lightSleep(micro_seconds, touch_wakeup)`, `deepSleep(micro_seconds,
touch_wakeup)`, `timerSleep(seconds | rtc_time_t | rtc_date_t+rtc_time_t)`,
`powerOff()`, plus battery/charging queries (`getBatteryLevel()`,
`getBatteryVoltage()`, `getBatteryCurrent()`, `isCharging()`) — all
battery-relevant and thus not applicable per the framing above, included
here only so a future reader doesn't have to re-discover that these exist
and then re-derive that they're the wrong tool for this device.

## Open questions for the next research pass

- Does auto-dim actually want a smooth PWM fade, or a simple day/night
  step change (matching the mockups' own binary Day/Night toggle)?
- Is there a real user-facing "how dim is too dim to read from across the
  room" constraint worth testing on hardware before picking a night
  brightness value?
- Should NVS write-during-power-loss actually get deliberately tested
  (pull the plug mid-`ConfigStore::save()`), or is ESP-IDF's own
  log-structured NVS design trustworthy enough to not need this
  project's own verification?
- Whether P4 light-sleep is even worth pursuing for CPU/heat reasons
  alone, independent of the battery question — needs a real power-draw
  measurement (the Tab5's own INA226 real-time power monitor, part of
  its charging/power-management circuitry, could plausibly answer this
  directly if wired into firmware, worth a follow-up look).

## Sources

- [M5Stack Tab5 docs](https://docs.m5stack.com/en/core/Tab5)
- [Tab5 Power Manager — m5-docs](https://docs.m5stack.com/en/arduino/m5tab5/power)
- [M5Unified Power Class — m5-docs](https://docs.m5stack.com/en/arduino/m5unified/power_class)
- [LCD Screen API (setBrightness) — m5-docs](http://docs.m5stack.com/en/api/core/lcd)
- [Sleep Modes — ESP32-P4 — ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/sleep_modes.html)
- [esp-hosted-mcu: ESP32-P4 function EV board](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/esp32_p4_function_ev_board.md) (same source `hardware.md` already cites for the C6 co-processor architecture)
