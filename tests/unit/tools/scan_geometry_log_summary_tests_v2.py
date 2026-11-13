"""User-run standard-library tests: python -B tests/unit/tools/scan_geometry_log_summary_tests_v2.py"""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("summary", ROOT / "tools/summarize_scan_geometry_log_v2.py")
module = importlib.util.module_from_spec(spec)
# Compile in memory so running this test does not create a source-tree __pycache__.
exec(compile(Path(spec.origin).read_text(encoding="utf-8"), spec.origin, "exec"), module.__dict__)


class LogSummaryTests(unittest.TestCase):
    def test_grouping_missing_values_and_no_sums(self):
        with tempfile.TemporaryDirectory(prefix="pcv-log-v2-") as directory:
            path = Path(directory) / "sample.jsonl"
            events = [{"event": "session", "configuration": "Release"}]
            for attempt, ms in [(1, 10), (2, 20)]:
                identity = {"input_revision": "1", "frame_id": "2", "attempt": str(attempt)}
                events.extend([
                    dict(identity, event="load_worker", status="success", worker_ms=ms, digest_ms=None),
                    dict(identity, event="display_ready", status="success", used_cache=True, input_ms=ms / 2),
                    dict(identity, event="task_finished", status="success", kind=3,
                         performance={"match_search_ms": ms, "seed_selection_ms": [2, 4]})])
            events += [{"event": "load_worker", "status": "failed", "worker_ms": 80},
                       {"event": "session", "configuration": "Debug"},
                       {"event": "load_worker", "status": "cancelled", "worker_ms": 5}]
            path.write_text("\n".join(json.dumps(event) for event in events) + "\ninvalid", encoding="utf-8")
            report = module.summarize([path])
            rows = report["groups"]
            row = next(r for r in rows if r["event"] == "load_worker" and r["status"] == "success")
            self.assertEqual((row["count"], row["median_ms"], row["min_ms"], row["max_ms"]), (2, 15, 10, 20))
            self.assertEqual(row["cache"], "hit")
            self.assertFalse(any(r["metric"] == "digest_ms" for r in rows))
            self.assertTrue(any(r["configuration"] == "Debug" and r["status"] == "cancelled" for r in rows))
            self.assertEqual(len(report["parse_issues"]), 1)
            self.assertFalse(any(r["metric"] == "sum_ms" for r in rows))

    def test_concatenated_sessions_do_not_share_cache_identity(self):
        with tempfile.TemporaryDirectory(prefix="pcv-log-v2-") as directory:
            path = Path(directory) / "sessions.jsonl"
            identity = {"input_revision": "1", "frame_id": "2", "attempt": "1"}
            events = [{"event": "session", "configuration": "Release"},
                      dict(identity, event="display_ready", used_cache=True),
                      dict(identity, event="load_worker", worker_ms=10),
                      {"event": "session", "configuration": "Debug"},
                      dict(identity, event="display_ready", used_cache=False),
                      dict(identity, event="load_worker", worker_ms=20)]
            path.write_text("\n".join(json.dumps(event) for event in events), encoding="utf-8")
            rows = module.summarize([path])["groups"]
            self.assertTrue(any(r["configuration"] == "Release" and r["cache"] == "hit" for r in rows))
            self.assertTrue(any(r["configuration"] == "Debug" and r["cache"] == "miss" for r in rows))

    def test_variant_and_verification_milestones(self):
        with tempfile.TemporaryDirectory(prefix="pcv-log-v3-") as directory:
            path=Path(directory)/"v3.jsonl"
            events=[{"event":"session","configuration":"Release","performance_version":"v3"},
                    {"event":"source_verified","status":"success","verified_ms":12,"digest_ms":3},
                    {"event":"index_worker","status":"success","performance":{"neighbor_prepare_ms":2}},
                    {"event":"index_ready","index_complete_ms":20,"selectable_ms":20}]
            path.write_text("\n".join(json.dumps(event) for event in events),encoding="utf-8")
            rows=module.summarize([path])["groups"]
            self.assertTrue(all(r["variant"]=="v3" for r in rows))
            self.assertTrue(any(r["metric"]=="verified_ms" and r["median_ms"]==12 for r in rows))
            self.assertTrue(any(r["metric"]=="neighbor_prepare_ms" for r in rows))
            self.assertTrue(all(r["variant"]=="baseline" for r in module.summarize([path],"baseline")["groups"]))

if __name__ == "__main__":
    unittest.main()
