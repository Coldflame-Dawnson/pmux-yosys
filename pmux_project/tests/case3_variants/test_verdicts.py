"""Acceptance policy checks independent of the Yosys smoke run."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from run_case3_variant_matrix import NAMES, adjudicate  # noqa: E402


def row(name, lut=13, dff=17, io=0, eqy="PASS"):
    return {"variant": name, "check": "PASS", "eqy": eqy,
            "lut": lut, "dff": dff, "io": io,
            "delta_lut": None, "verdict": "NOT_RUN"}


class Verdicts(unittest.TestCase):
    def test_all_thirteen_is_no_gain(self):
        rows = [row(name) for name in NAMES]
        self.assertEqual(adjudicate(rows, "REAL_VARIANTS"), [])
        self.assertEqual([item["verdict"] for item in rows[1:]], ["NO_GAIN"] * 5)

    def test_minimal_gain_and_rejections(self):
        rows = [row(name) for name in NAMES]
        rows[1]["lut"] = 12
        rows[2]["lut"], rows[2]["eqy"] = 11, "FAIL"
        rows[3]["lut"], rows[3]["dff"] = 11, 18
        rows[4]["lut"] = 11
        self.assertEqual(adjudicate(rows, "REAL_VARIANTS"), ["V2-A"])
        self.assertEqual(rows[1]["verdict"], "GAIN")
        self.assertEqual(rows[2]["verdict"], "REJECT_EQY")
        self.assertEqual(rows[3]["verdict"], "REJECT_RESOURCES")
        self.assertEqual(rows[4]["verdict"], "GAIN")

    def test_harness_cannot_claim_gain(self):
        rows = [row(name, lut=12) for name in NAMES]
        rows[0]["lut"] = 13
        self.assertEqual(adjudicate(rows, "HARNESS_SMOKE"), [])
        self.assertTrue(all(item["verdict"] == "HARNESS_ONLY" for item in rows[1:]))


if __name__ == "__main__":
    unittest.main()
