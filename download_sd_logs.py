#!/usr/bin/env python3
"""Download the camper controller's SD logs over its USB serial port."""

from __future__ import annotations

import argparse
import binascii
import sys
import time
from pathlib import Path

import serial


PREFIX = "@@CAMPER_SD_"


def safe_destination(root: Path, device_path: str) -> Path:
    relative = Path(device_path.lstrip("/"))
    if ".." in relative.parts:
        raise ValueError(f"unsafe device path: {device_path}")
    destination = (root / relative).resolve()
    if root.resolve() not in destination.parents:
        raise ValueError(f"path escapes destination: {device_path}")
    return destination


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM8", help="USB serial port")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("sd-download"),
        help="destination directory",
    )
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    current_path: str | None = None
    expected_size = 0
    payload = bytearray()
    completed = 0
    deadline = time.monotonic() + args.timeout

    print(f"Connecting to {args.port}...", flush=True)
    with serial.Serial(args.port, 115200, timeout=1, write_timeout=5) as port:
        port.dtr = False
        port.rts = False
        time.sleep(2)
        port.reset_input_buffer()
        started = False
        next_request = 0.0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if not started and now >= next_request:
                port.write(b"SD_EXPORT\n")
                port.flush()
                next_request = now + 5.0
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="ignore").strip()
            if not line.startswith(PREFIX):
                continue
            parts = line.split("|")

            if line.startswith("@@CAMPER_SD_ERROR|"):
                raise RuntimeError(line.split("|", 1)[1])
            if line == "@@CAMPER_SD_BEGIN|1":
                started = True
                print("Transfer started", flush=True)
            elif parts[0] == "@@CAMPER_SD_FILE" and len(parts) == 3:
                current_path = parts[1]
                expected_size = int(parts[2])
                payload.clear()
                print(f"  {current_path} ({expected_size} bytes)", flush=True)
            elif parts[0] == "@@CAMPER_SD_DATA" and len(parts) == 2:
                if current_path is None:
                    raise RuntimeError("received data before file header")
                payload.extend(bytes.fromhex(parts[1]))
            elif parts[0] == "@@CAMPER_SD_END_FILE" and len(parts) == 4:
                if current_path != parts[1]:
                    raise RuntimeError("file framing mismatch")
                reported_size = int(parts[2])
                reported_crc = int(parts[3], 16)
                actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
                if len(payload) != expected_size or len(payload) != reported_size:
                    raise RuntimeError(f"size check failed for {current_path}")
                if actual_crc != reported_crc:
                    raise RuntimeError(f"CRC check failed for {current_path}")
                destination = safe_destination(args.output, current_path)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(payload)
                completed += 1
                current_path = None
            elif parts[0] == "@@CAMPER_SD_DONE" and len(parts) == 2:
                expected_files = int(parts[1])
                if completed != expected_files:
                    raise RuntimeError(
                        f"received {completed} files; controller reported {expected_files}"
                    )
                print(
                    f"Downloaded {completed} files to {args.output.resolve()}",
                    flush=True,
                )
                return 0

        if not started:
            raise TimeoutError("controller did not answer; USB export firmware may not be flashed")
        raise TimeoutError("transfer did not finish before timeout")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, TimeoutError, serial.SerialException) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
