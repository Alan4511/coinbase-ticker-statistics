#!/usr/bin/env python3
"""Independently recompute exchange-time CSV statistics using Decimal and sorting.

This verification tool intentionally uses a simple full-window reference model,
not the C++ implementation's median partitions. Python is needed only to run this
optional evidence check, never to build or run the application.
"""

import argparse
import calendar
import csv
import json
import sys
from collections import Counter, defaultdict, deque
from datetime import datetime
from decimal import Decimal, localcontext
from pathlib import Path


COLUMNS = ["time", "symbol", "trade_id", "trade_price", "count", "mean", "median", "low", "high"]
NANOSECONDS_PER_SECOND = 1_000_000_000


def timestamp_ns(text):
    """Parse canonical UTC without losing the nanosecond window boundary."""
    if not text.endswith("Z"):
        raise ValueError("expected a UTC timestamp")
    seconds, separator, fraction = text[:-1].partition(".")
    if separator and (not fraction.isdigit() or len(fraction) > 9):
        raise ValueError("invalid timestamp fraction")
    instant = datetime.strptime(seconds, "%Y-%m-%dT%H:%M:%S")
    return calendar.timegm(instant.utctimetuple()) * NANOSECONDS_PER_SECOND + int(fraction.ljust(9, "0"))


def verify(path, config, require_expiration, relative_tolerance, absolute_tolerance):
    """Check every column's meaning and report observations/expirations by symbol."""
    window = config.get("window", {})
    duration = window.get("duration_seconds", 300) * NANOSECONDS_PER_SECOND
    expected_symbols = set(config["symbols"])
    active = defaultdict(deque)
    observations = Counter()
    expirations = Counter()
    first_times = {}
    last_times = {}
    rows = 0
    with path.open(newline="", encoding="utf-8") as stream, localcontext() as arithmetic:
        arithmetic.prec = 100
        reader = csv.DictReader(stream)
        if reader.fieldnames != COLUMNS:
            raise ValueError("unexpected CSV header")
        for line, row in enumerate(reader, start=2):
            try:
                if None in row or any(value is None for value in row.values()):
                    raise ValueError("wrong CSV column count")
                symbol = row["symbol"]
                if symbol not in expected_symbols:
                    raise ValueError("unconfigured symbol " + symbol)
                current = timestamp_ns(row["time"])
                if symbol in last_times and current < last_times[symbol]:
                    raise ValueError("time decreased within a symbol")
                first_times.setdefault(symbol, current)
                last_times[symbol] = current
                cutoff = current - duration
                queue = active[symbol]
                while queue and queue[0][0] <= cutoff:
                    queue.popleft()
                    expirations[symbol] += 1
                trade_id = int(row["trade_id"])
                if not 0 <= trade_id < 2 ** 64:
                    raise ValueError("trade ID outside uint64")
                if any(item[1] == trade_id for item in queue):
                    raise ValueError("a duplicate was emitted despite its configured policy")
                price = Decimal(row["trade_price"])
                if not price.is_finite() or price < 0:
                    raise ValueError("invalid price")
                queue.append((current, trade_id, price))
                prices = sorted(item[2] for item in queue)
                count = len(prices)
                middle = count // 2
                median = prices[middle] if count % 2 else (prices[middle - 1] + prices[middle]) / 2
                mean = sum(prices, Decimal(0)) / count
                expected = {
                    "count": str(count),

                }
                for field, value in expected.items():
                    if row[field] != value:
                        raise ValueError("{} expected {}, found {}".format(field, value, row[field]))
                for field, reference in (("mean", mean), ("median", median)):
                    actual = Decimal(row[field])
                    tolerance = max(absolute_tolerance, relative_tolerance * abs(reference))
                    if not actual.is_finite() or abs(actual - reference) > tolerance:
                        raise ValueError("{} differs beyond tolerance {}".format(field, tolerance))
                if Decimal(row["low"]) != prices[0] or Decimal(row["high"]) != prices[-1]:
                    raise ValueError("incorrect low/high")
                observations[symbol] += 1
                rows += 1
            except (ValueError, ArithmeticError) as error:
                raise ValueError("{} line {}: {}".format(path, line, error)) from error
    if set(observations) != expected_symbols:
        raise ValueError("missing configured symbols: " + repr(sorted(expected_symbols - set(observations))))
    if require_expiration and any(expirations[symbol] == 0 for symbol in expected_symbols):
        raise ValueError("capture does not demonstrate expiration for every configured symbol")
    print("PASS {}: {} rows independently verified".format(path.name, rows))
    for symbol in sorted(observations):
        span = Decimal(last_times[symbol] - first_times[symbol]) / NANOSECONDS_PER_SECOND
        print("  {}: observations={} expired={} observed_span_seconds={}".format(
            symbol, observations[symbol], expirations[symbol], span))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--require-expiration", action="store_true")
    parser.add_argument("--relative-tolerance", type=Decimal, default=Decimal("1e-12"))
    parser.add_argument("--absolute-tolerance", type=Decimal, default=Decimal("1e-15"))
    arguments = parser.parse_args()
    try:
        if any(not value.is_finite() or value < 0 for value in (arguments.relative_tolerance, arguments.absolute_tolerance)):
            raise ValueError("tolerances must be finite and nonnegative")
        with arguments.config.open(encoding="utf-8") as source:
            config = json.load(source)
        path = arguments.config.parent / config["output"]["path"]
        verify(path.resolve(), config, arguments.require_expiration,
               arguments.relative_tolerance, arguments.absolute_tolerance)
    except (OSError, ValueError, KeyError, ArithmeticError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
