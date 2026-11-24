"""Regression checks for the standalone HPCC tools (Python 3 and a C++17 compiler)."""

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HPCC = Path(__file__).resolve().parents[1]


class HpccToolsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.work = Path(cls.tmp.name)
        subprocess.run(["make", "-s", "-C", HPCC / "analysis", f"OUT={cls.work}"], check=True)

    def test_workloads(self):
        generator = HPCC / "traffic_gen"
        for path in generator.glob("*.txt"):
            with self.subTest(name=path.name):
                args = [sys.executable, generator / "traffic_gen.py", "-c", path, "--seed", "7",
                        "-n", "3", "--first-host", "1", "-b", "100G", "-l", "0.3", "-t", "0.0004"]
                output = self.work / "flows.txt"
                subprocess.run([*args, "-o", output], check=True)
                content = output.read_bytes()
                subprocess.run([*args, "-o", output], check=True)
                self.assertEqual(content, output.read_bytes())
                header, *records = content.decode().splitlines()
                self.assertEqual(int(header), len(records))
                self.assertTrue(records)
                previous = 2.0
                for record in records:
                    src, dst, pg, port, size, start = record.split()
                    self.assertTrue(1 <= int(src) <= 3 and 1 <= int(dst) <= 3 and src != dst)
                    self.assertTrue((pg, port) == ("3", "100") and int(size) > 0)
                    self.assertTrue(previous <= float(start) <= 2.0004)
                    previous = float(start)
                bins = subprocess.check_output([sys.executable, generator / "flow_bins.py", "-c", path])
                limits = [int(row.split()[0]) for row in bins.splitlines()]
                self.assertEqual(limits, sorted(set(limits)))

    def test_fct_analyzers(self):
        prefix = self.work / "FCT test"
        records = ("a b 1 100 1000 0 10 20\n"   # Slowdown is clamped to one.
                   "a b 2 200 2000 0 60 20\n"
                   "a b 3 100 3000 90 10 1\n")  # Finishes exactly at the cutoff.
        Path(f"{prefix}_one.txt").write_text(records)
        Path(f"{prefix}_two.txt").write_text(records.replace("60 20", "80 20"))
        limits = self.work / "bins.txt"
        limits.write_text("500 10\n1000 50\n2000 100\n")
        analyzers = ([sys.executable, HPCC / "analysis/fct_analysis.py"], [self.work / "fct_analysis"])
        for program in analyzers:
            args = [*program, "-p", prefix, "-c", "one,two", "-T", "100"]
            rows = subprocess.check_output([*args, "-S", limits, "-t", "2"], text=True).splitlines()
            self.assertEqual(rows[0].split(), ["0.100000", "500"] + ["nan"] * 6)
            self.assertEqual(rows[1].split(), ["0.500000", "1000"] + ["1.000"] * 6)
            self.assertEqual(rows[2].split(), ["1.000000", "2000"] + ["3.000"] * 3 + ["4.000"] * 3)
            for kind, size, first in (("0", "1000", "1.000"), ("1", "2000", "3.000")):
                rows = subprocess.check_output([*args, "-s", "60", "-t", kind], text=True).splitlines()
                self.assertEqual(rows[0].split()[2:], ["nan"] * 6)
                self.assertEqual(rows[1].split()[:3], ["1.000000", size, first])

    def test_trace_reader(self):
        trace = self.work / "trace.tr"
        trace.write_bytes(struct.pack("=II", 0, 0))  # No ports, zero window, no events.
        reader = [self.work / "trace_reader", trace]
        self.assertEqual(subprocess.check_output(reader), b"")
        for bad_filter in ("unknown=1", "time=1&", "(time>0", "queue=1,2"):
            self.assertNotEqual(subprocess.run([*reader, bad_filter], capture_output=True).returncode, 0)
        with trace.open("ab") as out:
            out.write(b"x")
        self.assertIn(b"Truncated", subprocess.run(reader, capture_output=True).stderr)


if __name__ == "__main__":
    unittest.main()
