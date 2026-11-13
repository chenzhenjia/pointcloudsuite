"""Summarize session JSONL without adding overlapping or parallel durations.

Usage: python summarize_scan_geometry_log_v2.py log.jsonl [more.jsonl ...]
Prints to stdout; redirect reports outside the source tree. Standard library only.
"""
import argparse
import collections
import json
import math
import statistics
from pathlib import Path


def summarize(paths, label=None):
    samples = collections.defaultdict(list)
    issues = []
    for path in paths:
        configuration = "unknown"
        session = 0
        variant = label or "legacy"
        cache = {}
        events = []
        for number, line in enumerate(Path(path).read_text(encoding="utf-8-sig").splitlines(), 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
                if not isinstance(event, dict):
                    raise ValueError("event must be an object")
            except (ValueError, TypeError) as error:
                issues.append(f"{path}:{number}: {error}")
                continue
            if event.get("event") == "session":
                configuration = event.get("configuration", "unknown")
                session += 1
                variant = label or event.get("performance_version", "legacy")
            events.append((session, configuration, variant, event))
            if event.get("event") == "display_ready" and "used_cache" in event:
                key = (session, event.get("input_revision"), event.get("frame_id"), event.get("attempt"))
                cache[key] = "hit" if event["used_cache"] else "miss"
        for session, configuration, variant, event in events:
            name = event.get("event", "unknown")
            if name in {"operation", "session", "task_started", "load_requested", "close_attempt", "session_closed"}:
                continue
            key = (session, event.get("task_input_revision", event.get("input_revision")), event.get("frame_id"), event.get("attempt"))
            status = event.get("status", "observed")
            if name == "task_finished":
                name += ":kind=" + str(event.get("kind", "unknown"))
            group = (configuration, cache.get(key, "unknown"), status, name, variant)
            values = dict(event)
            performance = event.get("performance", {})
            if isinstance(performance, dict):
                values.update(performance)
            for metric, value in values.items():
                if metric.endswith("_ms") and isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= 0:
                    samples[group + (metric,)].append(value)
            seed_ms = performance.get("seed_selection_ms", []) if isinstance(performance, dict) else []
            for value in seed_ms if isinstance(seed_ms, list) else []:
                if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= 0:
                    samples[group + ("per_seed_selection_ms",)].append(value)
    rows = []
    for group, values in sorted(samples.items()):
        rows.append(dict(zip(("configuration", "cache", "status", "event", "variant", "metric"), group),
                         count=len(values), median_ms=statistics.median(values), min_ms=min(values), max_ms=max(values)))
    return {"scope": "Each metric independently; no sums. first_frame_ms includes deferred visibility; use view/visible_wait_ms for navigation.",
            "groups": rows, "parse_issues": issues}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--label", help="Override variant label, e.g. baseline or optimized-v3")
    args = parser.parse_args()
    print(json.dumps(summarize(args.logs, args.label), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
