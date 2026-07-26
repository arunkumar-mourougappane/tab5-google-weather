# PlatformIO board config + CI

## Board target

There is no official *upstream* (`platformio/platform-espressif32`)
`m5stack-tab5` board entry, but `pioarduino` (the fork we use) does ship one —
`board = m5stack-tab5-p4`. This project ran on the generic `esp32-p4-evboard`
profile for a while first, which cost a string of manual workarounds
(`WiFi.setPins()`, USB build flags) for things the dedicated board just gets
right by default. Worth being precise about what that switch did and didn't
fix, though: it fixed WiFi bring-up being fragile (the generic board's
variant bakes in *wrong* SDIO pins to the C6 co-processor at static init,
not just "unset" ones), but it did **not** fix `WiFi.scanNetworks()` failing
outright — that took a separate platform version bump, below. See
[hardware.md](hardware.md) for the full story; it's a good example of two
real, independent bugs that looked at first like they might share one cause.

```ini
[env:tab5]
platform = https://github.com/pioarduino/platform-espressif32.git#55.03.39
board = m5stack-tab5-p4
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
build_flags =
    -DLV_CONF_INCLUDE_SIMPLE
    -DCORE_DEBUG_LEVEL=3
lib_deps =
    https://github.com/M5Stack/M5GFX.git
    https://github.com/M5Stack/M5Unified.git
    lvgl/lvgl@^9.2
```

`m5stack-tab5-p4.json`'s `extra_flags` already bake in `BOARD_HAS_PSRAM`,
`ARDUINO_USB_CDC_ON_BOOT=1`, and `ARDUINO_USB_MODE=1` — the three flags
documented below were all discovered and hand-added while still on the
generic board; kept here as history since the *reasons* they're needed are
still real, just no longer this project's problem to set manually.

**Verified the hard way — the platform version, not the board, is what fixed
`WiFi.scanNetworks()`.** Bumped from `#55.03.35` (ESP-IDF v5.5.1) to
`#55.03.39` (v5.5.4) after comparing against a working reference project on
the same hardware. `#55.03.35` was deliberately pinned to stay on IDF 5.5.1
specifically to dodge the MIPI-DSI backlight flicker regression IDF 5.5.2
introduced (below) — every tag past `.35` jumps straight to 5.5.2+, so this
trades a fixed, understood risk for an unfixed, understood benefit. Confirmed
on real hardware: scan works (real SSIDs returned) and no flicker observed —
but "clean on this unit" isn't a guarantee for every Tab5 out there, so watch
for flicker specifically after any future platform-version change, not just
this one. `#55.03.35` remains the documented-safe fallback if it ever
appears, at the cost of scan going back to broken (the manual SSID entry
field in the provisioning UI stays the primary path regardless of which
pin is active).

**Verified the hard way (Serial vs. the console) — took two flags, not one:**

1. Without `-DARDUINO_USB_CDC_ON_BOOT=1`, Arduino's `Serial` global silently
   maps to a plain UART0 that isn't wired to the USB port at all — our own
   `Serial.print()`/`printf()` calls go nowhere, and nothing typed into a
   monitor reaches the device either. Easy to misdiagnose as "no serial
   output at all," because it isn't: ESP-IDF's own boot/panic log (`E (941)
   ...`-style lines) travels over a *separate* path — the USB-Serial-JTAG
   controller's console — which keeps working regardless and is what a
   monitor tool connects to by default.
2. **Adding that flag alone still wasn't enough.** `arduino-esp32`'s
   `HardwareSerial.h` picks between two different USB peripherals based on a
   *second*, independent flag, `ARDUINO_USB_MODE`: left undefined (as it was
   — this board's variant doesn't set it either), `Serial` binds to "Native
   USB CDC" (a TinyUSB-backed USB-OTG peripheral), not the USB-Serial-JTAG
   controller the console already proved was connected. `-DARDUINO_USB_MODE=1`
   selects "Hardware CDC and JTAG" instead, binding `Serial` to that same,
   already-working controller. Confirms via the build itself, not just
   theory: flash usage *dropped* (~1.66MB → ~1.60MB) once the Native
   USB/TinyUSB code path was no longer being linked in.

**Verified the hard way:** the board's own `esp32-p4-evboard.json` declares
`"arduino": {"partitions": "default_16MB.csv"}` — but PlatformIO's builder
only reads a partitions filename from `build.partitions`, not `arduino.partitions`,
so that setting is silently ignored. Without `board_build.partitions` set
explicitly, the build falls back to a ~1.25MB default app partition — plenty
for the bare display/touch scaffold, but firmware linking in WiFi + TLS +
JSON + a QR encoder (the provisioning feature) hit 1.6MB and the build failed
at the link step with `program size ... greater than maximum allowed`, not a
compile error. `default_16MB.csv` gives two 6.25MB OTA app slots instead —
matches the Tab5's actual 16MB flash and leaves real headroom for the
dashboard/weather client still to come.

Verified requirement: the `#55.03.39` tag's `platform.json` declares
`"engines": {"platformio": ">=6.1.19"}` — one patch tighter than `#55.03.35`'s
`>=6.1.18`. PlatformIO Core 6.1.19 (the version on this dev machine) still
satisfies it, but exactly, with no patch releases to spare — worth rechecking
this if `pio run` ever starts complaining about the platformio version on an
older Core install.

**Verified the hard way:** `pio run` on this platform version hard-fails under
Python 3.14 with `ERROR: Python version must be between 3.10 and 3.13` — it's
a runtime check in the platform's own build scripts, not a PlatformIO Core
limit. macOS Homebrew defaults `python3` to whatever's newest (3.14 as of this
writing), so a system with only that installed can't build this project as-is.

The project keeps a `.venv/` (gitignored, `python3.12 -m venv .venv`) rather
than fighting the system `python3`:

```sh
python3.12 -m venv .venv
source .venv/bin/activate
pip install --upgrade platformio pyyaml
pio run -e tab5
```

Two things that bit us setting this up, worth knowing if `pio run` fails for
you too:
- **`pyyaml` isn't pulled in by `pip install platformio`**, but the
  `espressif32` builder's `arduino.py` framework script does `import yaml`
  directly and fails with `ModuleNotFoundError` if it's missing. Install it
  alongside `platformio`, in the CI workflow too (see below).
- The very first build attempt (from a throwaway `/tmp` venv, not this
  project's `.venv`) crashed mid-setup with `uv installation via pip failed
  with exit code -11` (segfault) while the `espidf`/`arduino` framework was
  bootstrapping its internal Python env. Running the exact same `pip install
  uv` command standalone succeeded immediately, and a clean retry from a
  fresh `.venv` didn't reproduce it at all — looked like transient resource
  contention, not a real incompatibility. If you hit it: just retry.

Notes:

- **`platform` is a fork (`pioarduino`), not upstream `platformio/platform-espressif32`.**
  Upstream doesn't yet carry ESP32-P4/IDF 5.5.x support; pioarduino does.
- **Pinned to `#55.03.39`** (IDF 5.5.4), not `latest` — still a deliberate
  pin, just no longer `#55.03.35`. That version was IDF 5.5.1, chosen
  specifically to dodge the MIPI-DSI backlight flicker regression IDF 5.5.2
  introduced; `.39` is past that version but was confirmed flicker-free on
  real hardware, traded for a `WiFi.scanNetworks()` fix — see
  [hardware.md](hardware.md) for the full story. Any further pin change
  needs a real device test for both symptoms, not just "does it compile."
- `board = m5stack-tab5-p4`, variant `m5stack_tab5` — the dedicated profile,
  not the generic `esp32-p4-evboard` this project started on.
- M5GFX/M5Unified pulled from GitHub `HEAD` rather than a pinned release for now,
  since Tab5 panel-driver support (the ST7123/ST7121 migration, see
  [hardware.md](hardware.md)) is still landing there — pin once a release tag
  post-dates that support.

## GitHub Actions

PlatformIO's own recommended workflow, adapted with `pio run -e tab5`:

```yaml
name: PlatformIO CI

on:
  push:
  pull_request:

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v6
      - uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio/.cache
          key: ${{ runner.os }}-pio-${{ hashFiles('platformio.ini') }}
          restore-keys: |
            ${{ runner.os }}-pio-
      - uses: actions/setup-python@v6
        with:
          python-version: "3.11"
      - name: Install PlatformIO Core
        run: pip install --upgrade platformio pyyaml
      - name: Build firmware
        run: pio run -e tab5
```

This only verifies compilation (no hardware-in-the-loop test runner exists for
this device), which is the honest scope for CI here — it catches build breaks
on every push/PR, nothing more.

## Sources

- [PlatformIO GitHub Actions integration docs](https://docs.platformio.org/en/latest/integration/ci/github-actions.html)
- [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32)
- [Using PlatformIO (VS Code) with Tab5 — M5Stack community](https://community.m5stack.com/topic/7586/using-platformio-vs-code-with-tab5)
- [amcchord/M5Tab-Macintosh platformio.ini](https://github.com/amcchord/M5Tab-Macintosh/blob/master/platformio.ini) — reference config, notes the flicker-regression pin
