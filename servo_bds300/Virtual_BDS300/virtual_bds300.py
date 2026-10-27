#!/usr/bin/env python3
"""Virtual BDS300 servo for validating the TC264 menu over UART.

The TC264 emits lines such as::

    BDS300 angle=45 min_us=1040 center_us=1540 max_us=2040 enable=1 pulse_us=1290 duty=645

Feed a captured line on stdin, or pass ``--serial COM5`` to read UART0 from a
USB-TTL adapter.  The simulator applies the same piecewise calibration as the
firmware and moves toward the requested angle over the BDS300's nominal 300 ms
full-range travel time.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from typing import Dict, Optional, TextIO


LINE_RE = re.compile(
    r"\bBDS300\s+"
    r"angle=(?P<angle>\d+)\s+"
    r"min_us=(?P<min_us>\d+)\s+"
    r"center_us=(?P<center_us>\d+)\s+"
    r"max_us=(?P<max_us>\d+)\s+"
    r"enable=(?P<enable>[01])\s+"
    r"pulse_us=(?P<pulse_us>\d+)"
    r"(?:\s+duty=(?P<duty>\d+))?"
)


@dataclass
class ServoState:
    target_angle: float = 90.0
    current_angle: float = 90.0
    min_us: int = 1040
    center_us: int = 1540
    max_us: int = 2040
    pulse_us: int = 1540
    enabled: bool = True
    last_update: float = 0.0


class VirtualBDS300:
    """Small, deterministic BDS300 model driven by firmware telemetry."""

    def __init__(self, travel_ms: int = 300) -> None:
        self.travel_ms = max(1, travel_ms)
        self.state = ServoState(last_update=time.monotonic())

    @staticmethod
    def _clamp_config(min_us: int, center_us: int, max_us: int) -> tuple[int, int, int]:
        min_us = max(500, min(2500, min_us))
        max_us = max(500, min(2500, max_us))
        if min_us >= max_us:
            min_us, max_us = 1040, 2040
        center_us = max(min_us, min(max_us, center_us))
        return min_us, center_us, max_us

    @staticmethod
    def pulse_to_angle(pulse_us: int, min_us: int, center_us: int, max_us: int) -> float:
        """Invert the firmware's 0..180 degree piecewise pulse mapping."""
        min_us, center_us, max_us = VirtualBDS300._clamp_config(
            min_us, center_us, max_us
        )
        if pulse_us <= min_us:
            return 0.0
        if pulse_us >= max_us:
            return 180.0
        if pulse_us <= center_us:
            return (pulse_us - min_us) * 90.0 / (center_us - min_us)
        return 90.0 + (pulse_us - center_us) * 90.0 / (max_us - center_us)

    def _advance(self, now: Optional[float] = None) -> None:
        now = time.monotonic() if now is None else now
        elapsed = max(0.0, now - self.state.last_update)
        self.state.last_update = now
        if not self.state.enabled:
            return
        max_step = 180.0 * elapsed * 1000.0 / self.travel_ms
        delta = self.state.target_angle - self.state.current_angle
        if abs(delta) <= max_step:
            self.state.current_angle = self.state.target_angle
        else:
            self.state.current_angle += max_step if delta > 0 else -max_step

    def apply_line(self, line: str) -> Optional[Dict[str, object]]:
        match = LINE_RE.search(line)
        if not match:
            return None
        values = {key: int(value) for key, value in match.groupdict(default="0").items()}
        self._advance()
        min_us, center_us, max_us = self._clamp_config(
            values["min_us"], values["center_us"], values["max_us"]
        )
        self.state.min_us = min_us
        self.state.center_us = center_us
        self.state.max_us = max_us
        self.state.pulse_us = values["pulse_us"]
        self.state.enabled = bool(values["enable"])
        if self.state.enabled:
            self.state.target_angle = max(
                0.0,
                min(180.0, self.pulse_to_angle(self.state.pulse_us, min_us, center_us, max_us)),
            )
        self._advance()
        return self.status()

    def status(self) -> Dict[str, object]:
        self._advance()
        return {
            "model": "BDS300",
            "enabled": self.state.enabled,
            "target_angle_deg": round(self.state.target_angle, 2),
            "current_angle_deg": round(self.state.current_angle, 2),
            "pulse_us": self.state.pulse_us if self.state.enabled else 0,
            "min_us": self.state.min_us,
            "center_us": self.state.center_us,
            "max_us": self.state.max_us,
        }


def _print_status(status: Dict[str, object], output: TextIO) -> None:
    print(json.dumps(status, ensure_ascii=False, separators=(",", ":")), file=output, flush=True)


def _list_ports() -> list[tuple[str, str]]:
    """Return currently enumerated serial ports as (device, description)."""
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError as exc:
        raise SystemExit("serial mode needs pyserial: python -m pip install pyserial") from exc
    return [(port.device, port.description or "unknown device") for port in list_ports.comports()]


def _print_ports(output: TextIO) -> None:
    ports = _list_ports()
    if not ports:
        print("No serial ports found. Connect the USB-TTL adapter and check its driver.", file=output)
        return
    print("Available serial ports:", file=output)
    for device, description in ports:
        print(f"  {device}: {description}", file=output)


def _resolve_auto_port() -> str:
    ports = _list_ports()
    if len(ports) == 1:
        return ports[0][0]
    if not ports:
        raise SystemExit(
            "No serial ports found; connect the USB-TTL adapter, then run "
            "python .\\Virtual_BDS300\\virtual_bds300.py --list-ports"
        )
    devices = ", ".join(device for device, _ in ports)
    raise SystemExit(f"Multiple serial ports found ({devices}); choose one with --serial COMx")


def _stdin_loop(model: VirtualBDS300, output: TextIO) -> None:
    for line in sys.stdin:
        status = model.apply_line(line)
        if status is not None:
            _print_status(status, output)


def _serial_loop(model: VirtualBDS300, port: str, baud: int, output: TextIO) -> None:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit("serial mode needs pyserial: python -m pip install pyserial") from exc
    if port.lower() == "auto":
        port = _resolve_auto_port()
    try:
        connection = serial.Serial(port=port, baudrate=baud, timeout=0.1)
    except Exception as exc:  # pyserial exposes platform-specific exceptions
        print(f"Cannot open {port}: {exc}", file=sys.stderr)
        _print_ports(sys.stderr)
        raise SystemExit(
            f"Choose one of the listed ports with --serial COMx; "
            f"if {port} should exist, check the cable, driver, and whether another program is using it."
        ) from exc
    with connection:
        last_report = 0.0
        while True:
            raw = connection.readline()
            if raw:
                status = model.apply_line(raw.decode("ascii", errors="ignore"))
                if status is not None:
                    _print_status(status, output)
                    last_report = time.monotonic()
            elif model.state.enabled and abs(model.state.current_angle - model.state.target_angle) > 0.01:
                # Keep the terminal useful while the virtual horn travels to
                # its target; stop reporting once it has settled.
                now = time.monotonic()
                if now - last_report >= 0.05:
                    _print_status(model.status(), output)
                    last_report = now


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--serial", metavar="PORT", help="UART0 port, or auto when exactly one port exists")
    source.add_argument("--stdin", action="store_true", help="read telemetry lines from stdin (default)")
    parser.add_argument("--list-ports", action="store_true", help="list detected serial ports and exit")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate (default: 115200)")
    parser.add_argument("--travel-ms", type=int, default=300, help="0..180 travel time (default: 300)")
    args = parser.parse_args()
    if args.list_ports:
        _print_ports(sys.stdout)
        return 0
    model = VirtualBDS300(travel_ms=args.travel_ms)
    if args.serial:
        _serial_loop(model, args.serial, args.baud, sys.stdout)
    else:
        _stdin_loop(model, sys.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
