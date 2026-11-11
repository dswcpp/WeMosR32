#!/usr/bin/env python3
"""Forward normalized quotes from LAN relay to STM32/GD32 via UART.

Requires: pip install pyserial
Example (Windows):
  python bridge_uart.py --port COM3 --symbol 000001 --interval 5
Example (Linux):
  python bridge_uart.py --port /dev/ttyUSB0 --symbol 000001
"""
import argparse
import time
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import urlopen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--symbol", default="000001")
    ap.add_argument("--interval", type=float, default=5.0)
    ap.add_argument("--relay", default="http://127.0.0.1:8765")
    args = ap.parse_args()
    if not (args.symbol.isascii() and args.symbol.isdigit() and len(args.symbol) == 6):
        ap.error("symbol must be six digits")
    if args.interval < 1.0:
        ap.error("minimum interval is 1 second")

    try:
        import serial
    except ImportError as exc:
        raise SystemExit("Install pyserial: pip install pyserial") from exc

    uri = args.relay.rstrip("/") + "/quote?" + urlencode({"symbol": args.symbol})
    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        print(f"Forwarding {args.symbol} -> {args.port} @ {args.baud}")
        while True:
            started = time.monotonic()
            try:
                with urlopen(uri, timeout=6) as resp:
                    if resp.status != 200:
                        raise URLError("non-200 status")
                    line = resp.read(100)
                if not line.startswith(b"Q,") or not line.endswith(b"\n"):
                    raise ValueError("bad relay frame")
                ser.write(line)
                print(line.decode("ascii").strip())
            except (OSError, ValueError, URLError) as exc:
                print(f"Fetch/serial error: {exc}")
            time.sleep(max(0.0, args.interval - (time.monotonic() - started)))


if __name__ == "__main__":
    main()
