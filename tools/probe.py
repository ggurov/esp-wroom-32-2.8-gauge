#!/usr/bin/env python3
"""
Identify the ESP32-2432S028R ("CYD") on this machine.

The board's CH340 has DTR wired to GPIO0 and RTS to EN, so unlike the round
board it can be reset into the ROM bootloader in software - this script just
probes every likely serial port with the normal reset sequence.

Run:  python tools/probe.py
"""

from __future__ import annotations

import subprocess
import sys

try:
    from serial.tools import list_ports
except ImportError:
    print("pyserial is missing.  python -m pip install -r tools/requirements.txt",
          file=sys.stderr)
    raise SystemExit(2)

# WCH bridges: (vid, pid, label)
KNOWN_BRIDGES = {
    (0x1A86, 0x55D3): "CH343",
    (0x1A86, 0x7523): "CH340",
    (0x1A86, 0x55D4): "CH343P",
    (0x10C4, 0xEA60): "CP2102",
    (0x0403, 0x6001): "FT232R",
}

VID_TOKENS = {"VID_1A86", "VID_303A", "VID_10C4", "VID_0403"}


def candidate_ports() -> list[tuple[str, str]]:
    found: list[tuple[str, str]] = []
    for p in list_ports.comports():
        label = KNOWN_BRIDGES.get((p.vid or 0, p.pid or 0))
        if label is None:
            hwid = (p.hwid or "").upper()
            if not any(tok in hwid for tok in VID_TOKENS):
                continue
            label = "usb-serial"
        found.append((p.device, f"{label}  {p.description or ''}".strip()))
    return found


def probe(port: str) -> bool:
    print(f"\n--- probing {port} ---")
    cmd = [sys.executable, "-m", "esptool", "--port", port, "flash-id"]
    try:
        rc = subprocess.call(cmd)
    except KeyboardInterrupt:
        return False
    return rc == 0


def main() -> int:
    ports = candidate_ports()
    if not ports:
        print("No USB serial adapters found. Is the board plugged in?", file=sys.stderr)
        return 1

    print("USB serial adapters:")
    for dev, desc in ports:
        print(f"  {dev:<8} {desc}")

    for dev, _ in ports:
        if probe(dev):
            print(f"\nOK: {dev} is an Espressif chip and can be flashed:")
            print(f"    tools\\idf.bat -p {dev} flash monitor")
            return 0

    print("\nNo ESP32 responded.  Try another cable or USB port, and remember "
          "that opening a port resets this board.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
