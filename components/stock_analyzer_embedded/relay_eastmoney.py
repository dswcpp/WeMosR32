#!/usr/bin/env python3
"""Experimental A-share quote adapter -> normalized C99 wire format.

Usage:
    python relay_eastmoney.py --bind 0.0.0.0 --port 8765
    curl 'http://127.0.0.1:8765/quote?symbol=000001'

This is for a trusted private LAN only. Upstream Eastmoney endpoint is
unofficial/undocumented, may change/disappear or be delayed. Check its terms.
Do NOT use for trading or treat provider data as exchange-grade real time.
"""
import argparse
from datetime import datetime, timezone, timedelta
from decimal import Decimal, InvalidOperation
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import re
from urllib.error import URLError
from urllib.parse import parse_qs, urlparse, urlencode
from urllib.request import Request, urlopen

CST = timezone(timedelta(hours=8))
SYMBOL = re.compile(r"^[0-9]{6}$")


def exchange(symbol):
    # Common Shanghai / Shenzhen A-shares, GEM and STAR.
    if symbol.startswith(("60", "68")):
        return "1"
    if symbol.startswith(("00", "30")):
        return "0"
    raise ValueError("supported: SH 60/68 or SZ 00/30 six-digit A-share codes")


def provider_time(value):
    """Accept Unix seconds/milliseconds or YYYYMMDDHHMM[SS] China time."""
    if value is None or isinstance(value, bool):
        raise ValueError("upstream quote time missing")
    s = str(value).strip()
    if not s.isdigit():
        raise ValueError("invalid upstream quote time")
    if len(s) == 10 and 1577836800 <= int(s) <= 4102444800:
        return int(s)
    if len(s) == 13 and 1577836800000 <= int(s) <= 4102444800000:
        return int(s) // 1000
    if len(s) in (12, 14):
        fmt = "%Y%m%d%H%M%S" if len(s) == 14 else "%Y%m%d%H%M"
        return int(datetime.strptime(s, fmt).replace(tzinfo=CST).timestamp())
    raise ValueError("unknown upstream quote timestamp format")


def normalize(symbol, data):
    if not isinstance(data, dict):
        raise ValueError("upstream data missing")
    if data.get("f57") != symbol:
        raise ValueError("upstream symbol mismatch")
    try:
        price = Decimal(str(data["f43"]))
        lots = Decimal(str(data["f47"]))
    except (InvalidOperation, KeyError):
        raise ValueError("upstream price/volume invalid") from None
    if not price.is_finite() or price <= 0 or price > 100000000 or \
            not lots.is_finite() or lots < 0 or lots != lots.to_integral_value():
        raise ValueError("upstream price/volume out of range")

    # Common Eastmoney f47 unit: 100-share lots ("手"). Verify with provider.
    shares = int(lots) * 100
    timestamp = provider_time(data.get("f86"))
    # fltt=2: f43 is normally in CNY, not cent units.
    payload = f"Q,{symbol},{timestamp},{price:f},{shares}"
    xor = 0
    for byte in payload.encode("ascii"):
        xor ^= byte
    return f"{payload}*{xor:02X}\n"


def fetch_quote(symbol):
    url = "https://push2.eastmoney.com/api/qt/stock/get?" + urlencode({
        "secid": f"{exchange(symbol)}.{symbol}",
        "fltt": "2", "invt": "2", "fields": "f43,f47,f57,f86"
    })
    req = Request(url, headers={
        "User-Agent": "Mozilla/5.0 EmbeddedStockAnalyzer/1.0",
        "Referer": "https://quote.eastmoney.com/",
        "Accept": "application/json",
    })
    with urlopen(req, timeout=5) as resp:
        obj = json.load(resp)
    return normalize(symbol, obj.get("data") if isinstance(obj, dict) else None)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path != "/quote":
            return self.send_error(404)
        symbols = parse_qs(parsed.query).get("symbol", [])
        if len(symbols) != 1 or not SYMBOL.fullmatch(symbols[0]):
            return self.send_error(400, "need six-digit symbol")
        try:
            result = fetch_quote(symbols[0]).encode("ascii")
        except (ValueError, KeyError, URLError, TimeoutError, OSError,
                json.JSONDecodeError) as exc:
            return self.send_error(502, str(exc)[:128])
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=us-ascii")
        self.send_header("Content-Length", str(len(result)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(result)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bind", default="127.0.0.1", help="0.0.0.0 for LAN")
    ap.add_argument("--port", default=8765, type=int)
    args = ap.parse_args()
    with ThreadingHTTPServer((args.bind, args.port), Handler) as server:
        print(f"Relay at http://{args.bind}:{args.port}/quote?symbol=000001")
        server.serve_forever()


if __name__ == "__main__":
    main()
