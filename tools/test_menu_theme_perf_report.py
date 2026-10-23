import argparse
import subprocess
import sys
import unittest
import menu_theme_perf_report as report

METRICS_BINARY = None

def window(seq=1, generation=3):
    return f"[menu_theme_perf] schema=1 type=window seq={seq} generation={generation} roots=1 dropped=0 started_ns=10 ended_ns=20\n"


def metric(seq=1):
    return f"[menu_theme_perf] schema=1 type=metric seq={seq} generation=3 name=Apply count=2 failed=0 inclusive_ns=6000000 self_ns=4000000 max_ns=5000000 hist=0,0,1,0,0,1,0,0,0,0,0,0\n"


class ReportTests(unittest.TestCase):
    def test_actual_cpp_emitter(self):
        if not METRICS_BINARY:
            self.skipTest("pass --metrics-selftest for C++ emitter integration")
        output = subprocess.check_output([METRICS_BINARY, "--log-fixture"], text=True, timeout=30)
        g = report.summarize(output.splitlines())["generations"][0]
        self.assertEqual(g["generation"], 7)
        self.assertEqual(g["metrics"]["Ready"]["inclusive_ns"], 9000)
        self.assertEqual(g["metrics"]["Ready"]["self_ns"], 7000)
        self.assertEqual(g["metrics"]["NativeColorSetter"]["count"], 1)
        self.assertEqual(g["metrics"]["RowPendingDelay"]["kind"], "latency")
        self.assertEqual(g["counters"]["SourceRecaptures"], 2)
        self.assertEqual(g["metrics"]["QualifyCallback"]["inclusive_ns"], 200)
        self.assertEqual(g["metrics"]["QualifyCallback"]["self_ns"], 30)
        self.assertEqual(g["metrics"]["QualifyMapping"]["inclusive_ns"], 30)
        self.assertEqual(g["metrics"]["FactsCapture"]["inclusive_ns"], 90)
        self.assertEqual(g["metrics"]["FactsObjectSnapshot"]["count"], 1)

    def test_multiline_logger_and_merge(self):
        lines = ["2026-10-22 [info] unrelated\n", "timestamp [info] " + window(), metric(),
                 window(2), metric(2),
                 "[menu_theme_perf] schema=1 type=counter seq=2 generation=3 name=CaptureHits value=7\n",
                 "[menu_theme_perf] schema=1 type=slow seq=2 generation=3 name=Apply ns=5000000 row=42 index=8 setup=99\n"]
        result = report.summarize(lines)
        g = result["generations"][0]
        self.assertEqual(g["metrics"]["Apply"]["count"], 4)
        self.assertEqual(g["metrics"]["Apply"]["self_ns"], 8000000)
        self.assertEqual(g["metrics"]["Apply"]["p95_upper_us"], 8000)
        self.assertEqual(g["counters"]["CaptureHits"], 7)
        self.assertEqual(g["slow"][0]["index"], 8)
        self.assertIn("Apply", report.text_report(result))

    def test_histogram_overflow(self):
        line = metric().replace("Apply", "RowPendingDelay").replace("hist=0,0,1,0,0,1,0,0,0,0,0,0", "hist=0,0,0,0,0,0,0,0,0,0,0,2")
        m = report.summarize([window(), line])["generations"][0]["metrics"]["RowPendingDelay"]
        self.assertIsNone(m["p95_upper_us"])
        self.assertEqual(m["kind"], "latency")

    def test_reject_incomplete_or_ambiguous_evidence(self):
        for lines in ([metric()], [window(), window()], [window(), metric(), metric()],
                      [window(), metric().replace("generation=3", "generation=4")],
                      [window(), metric().replace("count=2", "count=3")],
                      [window().replace("schema=1", "schema=2")],
                      [window(), metric().replace("self_ns=4000000", "self_ns=7000000")],
                      [window(), metric().replace("count=2", "count=-2")], []):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                report.summarize(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--metrics-selftest")
    args, remaining = parser.parse_known_args()
    METRICS_BINARY = args.metrics_selftest
    unittest.main(argv=[sys.argv[0], *remaining])
