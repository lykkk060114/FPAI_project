#!/usr/bin/env python3
"""Summarize stage wall/CPU times. Post/bridge durations are not end-to-end latency."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

def percentile(values, q):
    ordered = sorted(values)
    index = (len(ordered) - 1) * q
    low = int(index)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+', type=Path)
    parser.add_argument('--skip', type=int, default=0, help='Ignore initial rows of each file')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.skip < 0:
        parser.error('--skip must be nonnegative')
    reports = []
    for path in args.files:
        with path.open() as handle:
            reader = csv.DictReader(handle)
            columns = reader.fieldnames or []
            raw = list(reader)[args.skip:]
        valid = [r for r in raw if None not in r and all(r.get(c) is not None for c in columns)]
        stages = {}
        for column in columns:
            if not column.endswith('_ms') or column.startswith('unused_'):
                continue
            values = []
            for row in valid:
                try:
                    v = float(row[column])
                    if math.isfinite(v) and v >= 0:
                        values.append(v)
                except (ValueError, TypeError):
                    pass
            if values:
                stages[column] = dict(samples=len(values), mean=statistics.mean(values),
                                      p50=percentile(values, .5), p95=percentile(values, .95), max=max(values))
        rate = None
        if len(valid) > 1:
            try:
                times = [int(row['steady_ns']) for row in valid]
                seconds = (times[-1] - times[0]) / 1e9
                if seconds > 0:
                    rate = (len(valid) - 1) / seconds
            except (ValueError, KeyError):
                pass
        reports.append(dict(file=str(path), rows=len(valid), incomplete_rows=len(raw)-len(valid),
                            observed_record_rate_hz=rate,
                            uart_sent_rows=sum(r.get('uart_sent') == '1' for r in valid), stages_ms=stages))
    report = dict(scope='CSV stage timing; thread CPU excludes ORT worker threads; record rate is not sensor FPS',
                  skip_rows_per_file=args.skip, files=reports)
    data = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data)
    print(data, end='')

if __name__ == '__main__':
    main()
