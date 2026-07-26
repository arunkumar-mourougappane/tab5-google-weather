# PlatformIO board config + CI

## Board target

There is no official upstream `m5stack-tab5` board entry yet. The working setup
used by existing community Tab5 projects:

```ini
[env:tab5]
platform = https://github.com/pioarduino/platform-espressif32.git#55.03.35
board = esp32-p4-evboard
board_build.mcu = esp32p4
board_build.flash_mode = qio
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
build_flags =
    -DBOARD_HAS_PSRAM
lib_deps =
    https://github.com/M5Stack/M5GFX.git
    https://github.com/M5Stack/M5Unified.git
    lvgl/lvgl@^9.2
```

Verified requirement: the pinned `#55.03.35` tag's `platform.json` declares
`"engines": {"platformio": ">=6.1.18"}` — PlatformIO Core 6.1.19 (the version on
this dev machine) satisfies that with one patch release to spare.

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
- **Pinned to `#55.03.35`** (IDF 5.5.1), not `latest`. IDF 5.5.2 introduced a
  MIPI-DSI backlight flicker regression on this specific panel — see
  [hardware.md](hardware.md). Bumping this pin later needs a real device test,
  not just "does it compile."
- `board = esp32-p4-evboard` because Tab5 is built on the same ESP32-P4 EV-board
  reference design; there's no Tab5-specific board JSON.
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
