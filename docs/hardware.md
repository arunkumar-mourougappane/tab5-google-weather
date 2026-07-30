# M5Stack Tab5 hardware

## SoC

- **Espressif ESP32-P4** — dual-core RISC-V, 400 MHz. No built-in radio (no WiFi/BT).
- **32 MB PSRAM, 16 MB flash.**
- Has a **PPA (Pixel Processing Accelerator)** + DMA2D peripheral for hardware
  fill/blend/rotate/scale — see [rendering.md](rendering.md) for why we're not
  depending on it for v1.

## WiFi: the ESP32-C6 co-processor

The P4 has no radio, so the Tab5 carries a second chip, an **ESP32-C6**, wired to
the P4 over **SDIO**, running Espressif's `esp-hosted` firmware. The C6 acts as a
dumb WiFi/BT adapter; the P4 drives it through `esp_wifi_remote`, so from
application code (Arduino `WiFi.h`) it behaves like a normal on-chip WiFi stack
*once the link is up*.

### The real fix: use the dedicated board, not the generic one

This project started on `board = esp32-p4-evboard` (the Espressif P4 EV-board
reference design), because at the time no Tab5-specific board profile seemed
to exist. **One does: `board = m5stack-tab5-p4`** (variant `m5stack_tab5`),
already present in this project's pinned `pioarduino` install — no platform
version bump needed, just the board name. Found by comparing against a
sibling project on the same hardware that had WiFi working cleanly with none
of the workarounds below.

The generic board's variant (`esp32p4`) turned out to be the actual root
cause of everything in this section, not just "doesn't know Tab5's wiring."
Both variants define `BOARD_HAS_SDIO_ESP_HOSTED` with a *baked-in, static*
`sdio_pin_config_t` (`esp32-hal-hosted.c`) — but with different pins:

| Pin | `esp32p4` (generic, wrong for Tab5) | `m5stack_tab5` (correct) |
|---|---|---|
| CLK | 18 | 12 |
| CMD | 19 | 13 |
| D0–D3 | 14, 15, 16, 17 | 11, 10, 9, 8 |
| RESET | 54 | 15 |

That struct initializes before `setup()` ever runs. A runtime
`WiFi.setPins()` call (which is what this project used, and what M5Stack's
own Tab5 WiFi docs recommend for the generic board) overrides it *later*,
which got WiFi limping — softAP came up, basic HTTP served fine — without a
`setPins()` call ever being needed at all on the dedicated board, matching
the working reference project exactly. Its `extra_flags` in
`m5stack-tab5-p4.json` also already bake in `BOARD_HAS_PSRAM`,
`ARDUINO_USB_CDC_ON_BOOT=1`, and `ARDUINO_USB_MODE=1`, removing three more
manual `build_flags` this project had accumulated one hardware-debugging
session at a time (see [platformio-and-ci.md](platformio-and-ci.md)).

**Worth being precise about what this did and didn't fix, confirmed by
reflashing:** the board switch alone did *not* fix `WiFi.scanNetworks()` —
still deterministic `-2` afterward, identical to the generic board. The
tempting theory at the time ("a `setPins()` override landing after a
wrong-pinned struct's static init would plausibly be marginal enough to
explain a working AP but a failing scan") turned out to be wrong, or at
least incomplete. What actually fixed scan is the *platform version* bump
below — the board switch is real and worth keeping (matches the working
reference, removes redundant manual flags, is the technically correct
profile for this hardware regardless), it's just a different fix for a
different problem than scanning.

### What actually fixed scanning: the platform version, not the board

Comparing further against the working reference project surfaced two more
differences: its `platformio.ini` pins `platform =
.../platform-espressif32.git#55.03.39` (ours: `#55.03.35`) and sets
`-DCORE_DEBUG_LEVEL=3` (ours: unset).

The debug-level difference mattered immediately: with it enabled, the boot
log gained `[esp32-hal-hosted.c]` lines this project's earlier investigation
never saw — `Host firmware version: 2.12.8`, `Slave firmware version:
1.4.1`, `Version on Host is NEWER than version on co-processor`. Earlier in
this investigation, the *absence* of that exact warning in our boot log was
read as ruling out host/co-processor version skew as a cause. That
conclusion was never safe to draw — `CORE_DEBUG_LEVEL` gates ESP-IDF's
`[I]`/`[W]`-level logs at compile time, not just display; too low a level
and lines like this don't exist in the binary at all, regardless of what's
actually happening at runtime. (The reference project's own boot log shows
this same warning and scans successfully anyway, so version skew itself
isn't what blocks scanning either — but it means "the log doesn't show X"
was never valid evidence of anything here without first checking the debug
level could show X in the first place.)

The platform pin was the real fix. `#55.03.35` is pinned to ESP-IDF v5.5.1
specifically to avoid the MIPI-DSI backlight flicker regression documented
below as introduced in IDF 5.5.2 — every `pioarduino` tag after `.35`,
`.36` onward, jumps straight to IDF 5.5.2+ and stays there (`.39` is IDF
5.5.4). Bumping to `.39` — confirmed on real hardware — **fixes
`WiFi.scanNetworks()` outright** (real SSIDs returned, not just a
non-negative count) **with no backlight flicker observed on this unit**,
despite being past the version documented to introduce it. Accepted as a
deliberate tradeoff, not a free fix: flicker regressions can be
panel-revision-dependent (see the ST7123/ST7121 driver-IC migration note
below), so "clean on this unit" isn't a guarantee for every Tab5 out there.
If flicker ever shows up after a future platform bump, `#55.03.35` is the
documented-safe fallback — at the cost of `WiFi.scanNetworks()` going back
to broken, which is why the manual SSID entry field in provisioning stays
the primary path either way, not treated as a fallback for an edge case.

### Investigation history (logged against the generic `esp32-p4-evboard`, `#55.03.35`)

Without any pin configuration at all, every `WiFi.mode()`/`begin()`/`softAP()`
call failed outright — a repeating `sdmmc_init_ocr: send_op_cond (1)
returned 0x107` / `ensure_slave_bus_ready failed` / `ESP-Hosted link not yet
up` cascade in the serial log, ending in `esp_wifi_init: ESP_FAIL`. (That
`sdmmc`/`sdio_wrapper` naming is just the driver stack `esp-hosted` reuses
from SD-card support — nothing to do with an actual SD card or the Tab5's
card slot.) `WiFi.setPins()` with the pins above got past that, to a point.

From there, `WiFi.scanNetworks()` specifically never worked, through three
rounds of narrowing down the cause:
1. *Suspected AP+STA contention* — scanning while the softAP was already up
   and serving the setup page gave inconsistent results (empty once, then
   `WIFI_SCAN_FAILED` twice in a row, same device, no pattern). Worked
   around by scanning once in plain STA mode *before* `WiFi.softAP()` is
   ever called, caching the result for the provisioning session instead of
   rescanning per request — this part is worth keeping regardless of the
   board-profile fix, since avoiding needless concurrent radio use is just
   good practice.
2. *Suspected radio-not-ready timing* — even scanning before the AP existed,
   the very first attempt right after `WiFi.mode(WIFI_STA)` came back
   "completed, 0 networks" with no settling time at all. Added a 200ms delay
   after the mode switch and started retrying on a suspicious `0` result,
   not just an outright negative one.
3. With both of those addressed, the result became **deterministic**: `-2`
   (`WIFI_SCAN_FAILED`) on all 3 retry attempts, every time. Checked for the
   one documented ESP32-P4+`esp-hosted` failure signature that fits — host/
   co-processor firmware version skew, logged as `"Version on Host is NEWER
   than version on co-processor"` — not present in the boot log, ruling that
   out specifically.

At the time, this read as "basic WiFi works, scan specifically and
permanently doesn't, for a reason invisible without `esp-hosted` internals
access." The tempting explanation — a `WiFi.setPins()` override landing
after a wrong-pinned struct's static init, plausible enough for simple
traffic but not a scan's RPC round-trip — was a reasonable hypothesis given
what was known at the time, and is exactly the kind of theory worth writing
down even after being disproven: switching to the correctly-pinned dedicated
board (previous section) did *not* fix scanning, ruling it out. The actual
fix was unrelated to pin correctness entirely — see above.

### Reconnecting TLS too many times in a row crashes the SDIO driver

Confirmed on real hardware while bringing up the Weather API client
([google-weather-api.md](google-weather-api.md), `src/weather.cpp`): making
three back-to-back HTTPS requests to the same host, each with its own fresh
`WiFiClientSecure`/`HTTPClient` (connect, TLS handshake, request, `end()`,
destroy, repeat), reliably crashed on the *third* connection with
`assert failed: sdio_rx_get_buffer sdio_drv.c:953 (*buf)` — a hard fault
inside the esp-hosted SDIO driver itself, not application code.

Two plausible causes were ruled out before landing on the real one:

- **Not resource exhaustion.** `ESP.getFreeHeap()` logged right before the
  crash showed >100 KB free both times it happened.
- **Not timing/settling.** Adding a 750ms pause (pumping LVGL, same shape as
  the post-mode-switch delay that helped with `WiFi.scanNetworks()` above)
  between each request made no difference — crashed at the exact same point
  regardless.

What did fix it (for the reconnect case): reusing a single
`WiFiClientSecure` + `HTTPClient` (with `http.setReuse(true)`) across all
three calls instead of tearing down and reconnecting per request. All three
endpoints are the same host (`weather.googleapis.com`), so there's no reason
not to keep the connection open between them anyway — this cut three
separate TLS handshake+teardown cycles down to one. Root cause not fully
understood (this reads as a fixed-size SDIO RX buffer pool in the vendored
`esp-hosted` driver not being released cleanly on rapid disconnect/reconnect,
but that's inference from the symptom, not something traced into the driver
itself) — reusing the connection is a real fix for this project's original
crash, not confirmed to generalize to, say, hitting several different hosts
back-to-back.

**Update:** the same assert resurfaced later on real hardware even with the
connection reused across all three calls — this time partway through
`http.getString()` on the 3rd request (daily forecast) rather than at
connect time, with the first two requests (current conditions, hourly)
completing cleanly. So reconnect churn isn't the only trigger; sustained
SDIO RX volume across several requests in a tight sequence looks like a
second path to the same assert. A longer settle delay before the call
(`kWifiSettleMs` in `main.cpp`, 750ms → 2s) made no difference — confirmed
on hardware twice, identical crash at the identical instruction both times,
consistent with the earlier "not timing/settling" finding above.

**Resolved:** splitting the daily forecast into two smaller page requests
(4 days, then 3, via the API's `pageToken`/`nextPageToken` cursor
pagination — see `fetchDailyForecast()` in `src/weather.cpp`) instead of one
7-day call stopped the assert across three separate test boots on real
hardware. So response body size on the 3rd request, not request count or
timing, was the actual trigger — halving the largest of the three payloads
was enough. One of those three boots also hit an unrelated, non-fatal
`esp-aes: Failed to allocate memory for start alignment buffer` /
`IncompleteInput` on page 2 (a transient TLS/AES DMA-buffer allocation
blip) — that's a different failure mode, and the app already degrades
gracefully from it (keeps page 1's 4 days, logs the page-2 failure, keeps
running), not a regression of the SDIO assert.

**Regressed, then identified as a known, unresolved upstream bug — not
something fixable from this project's code.** After splitting the boot
flow into `uiTaskFn`/`netTaskFn` on separate cores
(docs/firmware-architecture.md's "Concurrency" section), the failure mode
got *worse*: `esp-aes: Failed to allocate memory for start alignment
buffer` started firing on the very first HTTPS request, on effectively
every boot, rather than the 2nd/3rd. Bracketing the heap right before task
creation and at the start of each task (`logHeapStats()` in `main.cpp`)
ruled out the task stacks themselves as the cause — `largest_dma` was
identical (110580 bytes) at all three points; creating the two tasks
consumed free heap but didn't fragment anything. What actually happens:
joining WiFi itself (`connectWifiOrRetryBoot()`) predictably consumes
~50KB of the largest contiguous block (known lwIP/WiFi-driver overhead,
unrelated to this project's code) — but even with **59KB still
contiguous and DMA-capable** afterward, the AES scratch allocation (which
only needs tens of bytes) still failed immediately. That ruled out "not
enough memory" as the explanation entirely.

Searching found the exact match: **[espressif/esp-hosted-mcu#144](https://github.com/espressif/esp-hosted-mcu/issues/144)**
— "ESP32P4 + ESP32C6 sdio_rx_get_buffer and transport_drv_sta_tx assert
failed" — the identical assert, on ESP-IDF/esp-hosted versions close to
what this project pins, reported while receiving HTTP responses. **Open,
no maintainer response, no fix**, as of this writing. The reporter tried
buffer-size and delay adjustments — the same category of things tried
above — without resolving it either.

There is a documented mitigation upstream in principle: newer esp-hosted
builds support relocating the SDIO transport's own buffer pool into PSRAM
instead of internal RAM (surfaced as ESPHome's `esp32_hosted: use_psram`
option), explicitly recommended when host firmware uses most of internal
RAM — exactly this project's symptom. It isn't reachable here, though:
checked the vendored `esp_hosted_transport_config.h`
(`framework-arduinoespressif32-libs`) and this version's public config
struct has no PSRAM flag, only `tx_queue_size`/`rx_queue_size`. Tried
reducing those instead — also a dead end: `esp32-hal-hosted.c`'s
`hostedInit()` (invoked automatically the first time `WiFi.mode()` runs)
unconditionally rebuilds its config from `INIT_DEFAULT_HOST_SDIO_CONFIG()`
and calls `esp_hosted_sdio_set_config()` itself, overwriting anything set
beforehand; reconfiguring after `esp_hosted_init()` has already run isn't
reliable either — the framework's own source has a telling comment on
that exact call, `// uncomment when second init is fixed`, i.e. Espressif's
own code acknowledges post-init reconfiguration is currently broken. Three
separate "the obvious lever doesn't work on this precompiled-libs target"
dead ends in a row (this one, the loop-stack-size build flag earlier in
this doc, and `JsonDocument::memoryUsage()` in firmware-architecture.md)
while chasing one confirmed-open upstream bug is where this stopped.

**Where this lands:** accepted as a known, currently-unfixable-from-here
upstream limitation, not chased further at the application level. The
failure is already survivable rather than catastrophic: NVS-persisted
config (`ConfigStore`) means the panic handler's automatic reboot just
replays the boot sequence and retries, not a bricked device. Revisit if a
future pioarduino platform bump brings in an esp-hosted version with the
PSRAM buffer-pool option exposed through the public config API — worth
checking against this issue specifically when evaluating any such bump,
not just the WiFi-scan/backlight-flicker regressions already tracked
above.

**Confirmed WiFi itself is not a factor, with explicit logging.** WiFi
association was suspected as a possible separate cause at one point
(a boot where the app never got past connecting). Added `logWifiState()`
(`WiFi.status()`/RSSI, called from `settleWifiLink()` before every fetch,
plus once right after the initial join) instead of inferring WiFi health
from `ssl_starttls_handshake()`'s position in the error trace. Confirmed
on hardware: `status=3 (connected)`, RSSI -38 to -41 dBm (strong signal),
logged before every single request across an entire boot where *every*
weather fetch failed at the TLS handshake stage — including the very
first attempt, with 59KB still contiguous and DMA-capable. WiFi is solid
throughout; the failure is squarely at the AES/TLS crypto-DMA layer, not
the WiFi link. That rules out the last remaining alternative explanation
and leaves the upstream esp-hosted bug above as the sole cause.

Two other things worth knowing before debugging WiFi issues on this hardware:

- The C6 runs its own **`esp_hosted` firmware image**, flashed to a dedicated
  partition, versioned independently of the app. Known-bad `esp_hosted` versions
  (e.g. v2.3.0 on some other P4 boards) have caused WiFi to drop after ~4 minutes;
  if we see periodic disconnects, that's the first thing to check, not our own code.
- SDIO is a single shared channel (40 MHz + HT20 tops out around ~36 Mbps
  real-world) multiplexed between WiFi data and RPC control traffic. Plenty for
  a weather API poll every few minutes; not a channel to worry about saturating.

## Display

- **5" IPS TFT, 1280×720 (720p), MIPI-DSI.**
- Touch: capacitive multi-touch, currently GT911 on most shipped units, though
  M5Stack has been migrating panel revisions (ILI9881C+GT911 → integrated
  ST7123 → ST7121 display/touch driver ICs) across production runs since late
  2025. Application-level behavior (resolution, multi-touch) is unaffected;
  it's handled inside M5GFX/M5Unified, so keeping those libraries reasonably
  current is what actually matters, not chasing the panel revision ourselves.
- A known ESP-IDF 5.5.2 regression causes MIPI-DSI backlight flicker on Tab5 —
  reason we pin the `pioarduino` platform version rather than tracking latest
  (see [platformio-and-ci.md](platformio-and-ci.md)).

## Other onboard peripherals (not used by this project, noted for completeness)

- SC2356 2 MP camera (MIPI-CSI)
- ES8388 audio codec + 1 W speaker, ES7210 dual-mic array with AEC
- Removable 2000 mAh battery
- Gyro, RS485, Thread/Zigbee radio (via the C6's 802.15.4), microSD, 3.5 mm jack

## Sources

- [M5Stack Tab5 docs](https://docs.m5stack.com/en/core/Tab5)
- [M5Stack Tab5 product page](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4)
- [esp-hosted-mcu: ESP32-P4 function EV board](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/esp32_p4_function_ev_board.md)
- [dmatking/esp32-p4-wifi-starter](https://github.com/dmatking/esp32-p4-wifi-starter) — P4+C6 architecture writeup
- [lboshuizen/crowpanel-p4-c6-sdio-ota](https://github.com/lboshuizen/crowpanel-p4-c6-sdio-ota) — esp_hosted version/disconnect-bug notes
- [ESP-IDF PPA peripheral guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html)
- [m5-docs: Tab5 WiFi (Arduino)](https://docs.m5stack.com/en/arduino/m5tab5/wifi) — the `setPins()` call and GPIO numbers
- [M5Stack community: Using WiFi on Tab5 with the Arduino IDE](https://community.m5stack.com/topic/7576/using-wifi-on-tab5-with-the-arduino-ide) — mixed reports on whether `setPins()` alone is sufficient
- [m5-docs: Tab5 ESP32-C6 WiFi module factory firmware restore](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi) — fallback if the C6's `esp-hosted` firmware itself is the problem
