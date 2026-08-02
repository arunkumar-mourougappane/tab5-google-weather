#!/usr/bin/env python3
"""Parses this project's serial log output into per-tag CSVs and (optionally)
time-series plots, for endurance-run analysis (see docs/roadmap.md and
[env:tab5-endurance] in platformio.ini).

Reads either a live serial port or an already-captured log file - same
parser either way, so a long unattended run can be analyzed as it goes or
reprocessed later from a saved capture. Not hardcoded to the "endurance"
tag specifically: every LOG_* line this project emits already follows a
`key=value key=value ...` convention within its message (see logging.h's
own macros and every call site), so this generically extracts key=value
pairs from *any* tagged line - endurance/heap/net/ui/weather, whatever's
present in the capture - rather than only understanding one log line
shape and discarding the rest.

Usage:
    # Live: tails a serial port, writes CSVs (and plots, unless --no-plot)
    # as it goes, until Ctrl+C.
    python3 tools/analyze_endurance_log.py --serial /dev/tty.usbmodem1101 --out run1/

    # Offline: reprocess an already-captured log file.
    python3 tools/analyze_endurance_log.py --input run1/raw.log --out run1/

Output layout under --out:
    raw.log              - every line seen, verbatim (live mode only - an
                            --input file is never rewritten over itself)
    <tag>.csv             - one row per parsed line for that tag, columns
                            are the union of keys seen for it
    plots/<tag>_<field>.png - one time-series plot per numeric field,
                              skipped if matplotlib isn't installed
"""
import argparse
import csv
import re
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

# Matches both of logging.cpp's logPrefix() formats:
#   "2026-08-02 14:24:47 UTC [I][endurance] cycle=1 screen=hourly ..."
#   "+12907ms [I][net] WiFi connected: ssid=... mac=... rssi=-59 dBm"
# (the uptime-ms form is used before NTP sync completes - see
# logSetTimeSynced()).
LINE_RE = re.compile(
    r"^(?:(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) UTC|\+(?P<uptime>\d+)ms)"
    r"\s\[(?P<level>[EWID])\]\[(?P<tag>[A-Za-z0-9_]+)\]\s(?P<rest>.*)$"
)
# key=value tokens anywhere in the rest of the line - deliberately not
# anchored to the whole line, since several LOG_* call sites mix prose
# with key=value pairs (e.g. "WiFi connected: ssid=%s ip=%s ... dBm").
KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s,]+)")


def coerce(value):
    """int if it parses as one, else float, else the raw string - keeps
    e.g. screen=hourly as text while cycle=42/free_heap=193044 become
    real numbers a plotting/analysis step can do arithmetic on."""
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return value


def parse_line(line, fallback_wallclock):
    """Returns (tag, row_dict) or None if the line doesn't match this
    project's own log format at all (garbled/interleaved lines happen on
    real hardware - see docs/hardware.md's logging notes - skipped
    rather than crashing the whole run over one bad line)."""
    m = LINE_RE.match(line.strip())
    if not m:
        return None
    tag = m.group("tag")
    row = {"level": m.group("level")}
    if m.group("ts"):
        row["timestamp"] = m.group("ts")
    else:
        # No synced wall-clock yet - falls back to when *this script* saw
        # the line, only for rows captured live (offline replay of an old
        # log has no meaningful "now"). Still keeps the device's own
        # uptime_ms, which is what actually matters for ordering/spacing
        # these early lines.
        row["timestamp"] = fallback_wallclock
        row["uptime_ms"] = int(m.group("uptime"))
    for key, value in KV_RE.findall(m.group("rest")):
        row[key] = coerce(value)
    return tag, row


class TagWriter:
    """One CSV per tag, columns unioned across all rows seen so far -
    written incrementally (not buffered in memory for the whole run),
    since this is meant to survive a multi-day capture."""

    def __init__(self, out_dir):
        self.out_dir = out_dir
        self._files = {}
        self._writers = {}
        self._columns = {}
        self.rows_by_tag = defaultdict(list)  # kept for the plotting pass

    def write(self, tag, row):
        self.rows_by_tag[tag].append(row)
        columns = self._columns.setdefault(tag, [])
        for key in row:
            if key not in columns:
                columns.append(key)
        if tag not in self._files:
            path = self.out_dir / f"{tag}.csv"
            self._files[tag] = open(path, "w", newline="", encoding="utf-8")
            self._writers[tag] = csv.DictWriter(self._files[tag], fieldnames=columns, restval="")
            self._writers[tag].writeheader()
        else:
            # A later row introduced a new column this tag's header
            # didn't have yet - restart the file with the wider column
            # set rather than silently dropping the new field. Cheap:
            # these files are small (one line per log message, not per
            # byte of a long capture).
            if self._writers[tag].fieldnames != columns:
                self._files[tag].close()
                path = self.out_dir / f"{tag}.csv"
                with open(path, newline="", encoding="utf-8") as f:
                    existing = list(csv.DictReader(f))
                self._files[tag] = open(path, "w", newline="", encoding="utf-8")
                self._writers[tag] = csv.DictWriter(self._files[tag], fieldnames=columns, restval="")
                self._writers[tag].writeheader()
                for r in existing:
                    self._writers[tag].writerow(r)
        self._writers[tag].writerow(row)
        self._files[tag].flush()

    def close(self):
        for f in self._files.values():
            f.close()


def run_offline(input_path, writer):
    with open(input_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            fallback = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
            parsed = parse_line(line, fallback)
            if parsed:
                writer.write(*parsed)


def run_live(port, baud, writer, raw_log_path):
    try:
        import serial
    except ImportError:
        print("error: pyserial is required for --serial (pip3 install pyserial)", file=sys.stderr)
        sys.exit(1)

    print(f"Reading {port} @ {baud} baud - Ctrl+C to stop and finalize.")
    with serial.Serial(port, baud, timeout=1) as ser, open(raw_log_path, "w", encoding="utf-8") as raw:
        while True:
            try:
                raw_bytes = ser.readline()
            except KeyboardInterrupt:
                break
            if not raw_bytes:
                continue
            line = raw_bytes.decode("utf-8", errors="replace")
            raw.write(line)
            raw.flush()
            fallback = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
            parsed = parse_line(line, fallback)
            if parsed:
                writer.write(*parsed)


def make_plots(writer, plots_dir):
    try:
        import matplotlib

        matplotlib.use("Agg")  # headless - this is a save-to-file tool, not an interactive viewer
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed - skipping plots (pip3 install matplotlib to enable). CSVs are still written.")
        return

    plots_dir.mkdir(exist_ok=True)
    for tag, rows in writer.rows_by_tag.items():
        if len(rows) < 2:
            continue  # a single point isn't a time series worth plotting
        numeric_fields = sorted(
            {k for row in rows for k, v in row.items() if isinstance(v, (int, float)) and k != "uptime_ms"}
        )
        for field in numeric_fields:
            xs, ys = [], []
            for i, row in enumerate(rows):
                if field in row and isinstance(row[field], (int, float)):
                    xs.append(i)
                    ys.append(row[field])
            if len(ys) < 2:
                continue
            fig, ax = plt.subplots(figsize=(10, 4))
            ax.plot(xs, ys, marker=".", linewidth=1)
            ax.set_title(f"{tag}: {field}")
            ax.set_xlabel("sample #")
            ax.set_ylabel(field)
            ax.grid(True, alpha=0.3)
            fig.tight_layout()
            out_path = plots_dir / f"{tag}_{field}.png"
            fig.savefig(out_path, dpi=120)
            plt.close(fig)
            print(f"wrote {out_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--serial", metavar="PORT", help="Serial port to tail live, e.g. /dev/tty.usbmodem1101")
    source.add_argument("--input", metavar="FILE", help="Already-captured log file to parse offline")
    parser.add_argument("--baud", type=int, default=115200, help="Matches platformio.ini's monitor_speed (default 115200)")
    parser.add_argument("--out", default="endurance_analysis", help="Output directory (default: ./endurance_analysis)")
    parser.add_argument("--no-plot", action="store_true", help="Skip plot generation, just write CSVs")
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    writer = TagWriter(out_dir)

    try:
        if args.serial:
            run_live(args.serial, args.baud, writer, out_dir / "raw.log")
        else:
            run_offline(args.input, writer)
    finally:
        writer.close()

    total = sum(len(rows) for rows in writer.rows_by_tag.values())
    print(f"Parsed {total} log lines across {len(writer.rows_by_tag)} tags into {out_dir}/")
    for tag, rows in sorted(writer.rows_by_tag.items()):
        print(f"  {tag}: {len(rows)} rows -> {tag}.csv")

    if not args.no_plot:
        make_plots(writer, out_dir / "plots")


if __name__ == "__main__":
    main()
