"""Summarize bounded schema-1 menu-theme performance windows (standard library only)."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

HISTOGRAM_US = (50, 250, 1000, 2000, 4000, 8000, 16000, 32000, 64000, 128000, 512000)
UINT64_MAX = (1 << 64) - 1
LATENCIES = {"ReadyToCommit", "RowPendingDelay", "PreparationQueueWait"}
TAG = "[menu_theme_perf] "


def number(fields, key):
    text = fields[key]
    if not re.fullmatch(r"[0-9]+", text) or int(text) > UINT64_MAX:
        raise ValueError(f"invalid unsigned field {key}")
    return int(text)


def summarize(lines):
    windows, generations = {}, {}
    for line_number, line in enumerate(lines, 1):
        if TAG not in line:
            continue
        try:
            if len(line) > 65536:
                raise ValueError("oversized metric line")
            fields = {}
            for item in line.split(TAG, 1)[1].split():
                key, value = item.split("=", 1)
                if key in fields:
                    raise ValueError(f"duplicate field {key}")
                fields[key] = value
            if fields["schema"] != "1":
                raise ValueError("unsupported performance schema")
            seq, generation = number(fields, "seq"), number(fields, "generation")
            kind = fields["type"]
            if kind == "window":
                if seq in windows:
                    raise ValueError("duplicate window sequence (concatenated runs?)")
                windows[seq] = {"generation": generation, "seen": set()}
                g = generations.setdefault(generation, {"generation": generation, "windows": 0,
                    "roots": 0, "dropped": 0, "metrics": {}, "counters": {}, "slow": []})
                g["windows"] += 1
                for key in ("roots", "dropped"):
                    g[key] += number(fields, key)
                if number(fields, "ended_ns") < number(fields, "started_ns"):
                    raise ValueError("backwards window interval")
                continue
            if seq not in windows or windows[seq]["generation"] != generation:
                raise ValueError("record has no matching window")
            name = fields["name"]
            if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", name):
                raise ValueError("invalid metric name")
            g = generations[generation]
            if kind in ("metric", "counter"):
                identity = (kind, name)
                if identity in windows[seq]["seen"]:
                    raise ValueError("duplicate metric/counter in window")
                windows[seq]["seen"].add(identity)
            if kind == "metric":
                values = {k: number(fields, k) for k in ("count", "failed", "inclusive_ns", "self_ns", "max_ns")}
                hist = fields["hist"].split(",")
                if len(hist) != len(HISTOGRAM_US) + 1:
                    raise ValueError("incorrect histogram shape")
                values["histogram"] = [number({"bucket": x}, "bucket") for x in hist]
                if sum(values["histogram"]) != values["count"]:
                    raise ValueError("histogram/count mismatch or saturation")
                if values["failed"] > values["count"] or values["self_ns"] > values["inclusive_ns"] \
                        or values["max_ns"] > values["inclusive_ns"]:
                    raise ValueError("inconsistent metric totals")
                target = g["metrics"].setdefault(name, {"count": 0, "failed": 0, "inclusive_ns": 0,
                    "self_ns": 0, "max_ns": 0, "histogram": [0] * len(hist),
                    "kind": "latency" if name in LATENCIES else "scope"})
                for key in ("count", "failed", "inclusive_ns", "self_ns"):
                    target[key] += values[key]
                target["max_ns"] = max(target["max_ns"], values["max_ns"])
                target["histogram"] = [a + b for a, b in zip(target["histogram"], values["histogram"])]
            elif kind == "counter":
                g["counters"][name] = g["counters"].get(name, 0) + number(fields, "value")
            elif kind == "slow":
                event = {"name": name, "ns": number(fields, "ns"), "setup": number(fields, "setup")}
                for key in ("row", "index"):
                    if not re.fullmatch(r"-?[0-9]+", fields[key]):
                        raise ValueError(f"invalid {key}")
                    event[key] = int(fields[key])
                g["slow"].append(event)
                g["slow"] = sorted(g["slow"], key=lambda x: x["ns"], reverse=True)[:8]
            else:
                raise ValueError(f"unknown record type {kind}")
        except (KeyError, ValueError) as error:
            raise ValueError(f"line {line_number}: {error}") from error
    if not windows:
        raise ValueError("no schema-1 performance windows; an instrumented runtime log is required")
    for g in generations.values():
        for item in g["metrics"].values():
            threshold = (item["count"] * 95 + 99) // 100
            total = 0
            item["p95_upper_us"] = None
            for i, value in enumerate(item["histogram"]):
                total += value
                if total >= threshold:
                    item["p95_upper_us"] = HISTOGRAM_US[i] if i < len(HISTOGRAM_US) else None
                    break
    return {"schema": 1, "generations": [generations[g] for g in sorted(generations)], "limits": [
        "Wall time, not CPU, GPU or FPS. Inclusive nested timings must not be added together.",
        "Self time excludes instrumented direct children, not uninstrumented work or scheduling pauses.",
        "Latency samples are not execution time; p95 is a histogram bucket upper bound, null means overflow.",
        "Dropped roots, depth overflow, clock failures or saturated counters indicate incomplete detail.",
        "Snapshots contain only completed scopes; abrupt process termination can lose the final window."]}


def text_report(report):
    lines = []
    for g in report["generations"]:
        lines.append(f"Generation {g['generation']}: windows={g['windows']} roots={g['roots']} dropped={g['dropped']}")
        lines.append(f"{'Scope/sample':28} {'Count':>8} {'Failed':>6} {'Incl ms':>12} {'Self ms':>12} {'Max ms':>10} {'p95 <=us':>10}")
        for name, m in sorted(g["metrics"].items(), key=lambda x: x[1]["self_ns"], reverse=True):
            bound = m["p95_upper_us"] if m["p95_upper_us"] is not None else ">512000"
            lines.append(f"{name:28} {m['count']:8} {m['failed']:6} {m['inclusive_ns']/1e6:12.3f} {m['self_ns']/1e6:12.3f} "
                         f"{m['max_ns']/1e6:10.3f} {str(bound):>10}")
        lines.extend(f"  {name}={value}" for name, value in sorted(g["counters"].items()))
        lines.extend(f"  slow {e['name']} {e['ns']/1e6:.3f}ms row={e['row']} index={e['index']} setup={e['setup']}"
                     for e in g["slow"])
    return "\n".join(lines + ["", *report["limits"]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true", help="print machine-readable summary instead of table")
    args = parser.parse_args()
    try:
        with args.log.open(encoding="utf-8", errors="replace") as source:
            report = summarize(source)
        with args.log.open("rb") as source:
            report["source_sha256"] = hashlib.file_digest(source, "sha256").hexdigest()
        print(json.dumps(report, ensure_ascii=False, indent=2) if args.json else text_report(report))
        return 0
    except (OSError, ValueError) as error:
        print(f"menu theme performance report: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
