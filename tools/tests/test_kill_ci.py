# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for kill_plan.py and kill_summary.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import kill_plan  # noqa: E402
import kill_summary  # noqa: E402


class PlanTest(unittest.TestCase):
    def test_shards_cover_every_iteration_once(self) -> None:
        for iterations, shards in [(10000, 10), (1000, 4), (100, 1), (7, 3), (3, 10), (1, 1)]:
            plan = kill_plan.split(iterations, shards, 200)
            covered = [i for shard in plan for i in range(shard["first"], shard["last"] + 1)]
            self.assertEqual(covered, list(range(iterations)), (iterations, shards))
            self.assertEqual([shard["index"] for shard in plan], list(range(len(plan))))

    def test_the_gate_is_ten_shards_of_a_thousand(self) -> None:
        plan = kill_plan.split(10000, 10, 200)
        self.assertEqual(len(plan), 10)
        self.assertTrue(all(shard["last"] - shard["first"] + 1 == 1000 for shard in plan))
        self.assertEqual(plan[0]["first"], 0)
        self.assertEqual(plan[-1]["last"], 9999)

    def test_the_nightly_is_four_shards_of_250(self) -> None:
        plan = kill_plan.split(1000, 4, 200)
        self.assertEqual([(s["first"], s["last"]) for s in plan], [(0, 249), (250, 499), (500, 749), (750, 999)])
        self.assertEqual({s["secondary"] for s in plan}, {50})

    def test_the_secondary_share_rounds_up(self) -> None:
        self.assertEqual(kill_plan.split(100, 3, 25)[0]["secondary"], 9)
        self.assertEqual(kill_plan.split(100, 1, 0)[0]["secondary"], 0)

    def test_nothing_to_run_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            kill_plan.split(0, 1, 0)
        with self.assertRaises(ValueError):
            kill_plan.split(10, 0, 0)

    def test_seed_is_normalised_or_drawn(self) -> None:
        self.assertEqual(kill_plan.normalise_seed("0x1F"), "0x000000000000001f")
        self.assertEqual(kill_plan.normalise_seed(" 255 "), "0x00000000000000ff")
        self.assertEqual(kill_plan.normalise_seed("0xFFFFFFFFFFFFFFFF"), "0xffffffffffffffff")
        drawn = kill_plan.normalise_seed("")
        self.assertRegex(drawn, r"^0x[0-9a-f]{16}$")
        self.assertNotEqual(drawn, kill_plan.normalise_seed(""))

    def test_a_seed_that_could_be_anything_else_is_refused(self) -> None:
        for bad in ["0x", "0x1ffffffffffffffff", "abc", "1; echo", "-1", "$(id)"]:
            with self.assertRaises(ValueError, msg=bad):
                kill_plan.normalise_seed(bad)

    def test_main_prints_the_outputs(self) -> None:
        import contextlib
        import io

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = kill_plan.main(["--seed", "0x10", "--iterations", "20", "--shards", "2", "--secondary", "4"])
        self.assertEqual(code, 0)
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0], "seed=0x0000000000000010")
        self.assertEqual(
            lines[1],
            'shards=[{"index":0,"first":0,"last":9,"secondary":2},{"index":1,"first":10,"last":19,"secondary":2}]')


RESULT = ("ej-kill result master_seed=0x0000000000000010 first={first} last={last} runs={runs} failures={failures} "
          "acknowledged_operations={ops} in_flight_present=3 replayed=1")


def write_summary(root: Path, name: str, *lines: str) -> None:
    folder = root / name
    folder.mkdir(parents=True)
    (folder / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


class SummaryTest(unittest.TestCase):
    def test_clean_run_adds_up_and_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_summary(root, "a", RESULT.format(first=0, last=499, runs=500, failures=0, ops=100000),
                          "ej-kill recovery result master_seed=0x10 iterations=25 killed_before_the_end=20")
            write_summary(root, "b", RESULT.format(first=500, last=999, runs=500, failures=0, ops=90000))
            text, ok = kill_summary.summarise(kill_summary.read([root]), "nightly", "0x10", 1000)
        self.assertTrue(ok)
        self.assertIn("| random kills | 2 | 1000 | 0 | 190000 |", text)
        self.assertIn("| recovery killed at random moments | 1 | 25 | 0 |", text)
        self.assertIn("1000 random-kill runs, no failures.", text)

    def test_one_failure_fails_the_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_summary(root, "a", RESULT.format(first=0, last=99, runs=100, failures=1, ops=5))
            text, ok = kill_summary.summarise(kill_summary.read([root]), "merge-queue", "0x10", 100)
        self.assertFalse(ok)
        self.assertIn("Failures", text)

    def test_a_failing_secondary_test_fails_the_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_summary(root, "a", RESULT.format(first=0, last=99, runs=100, failures=0, ops=5),
                          "ej-kill draft result master_seed=0x10 iterations=6 failures=2")
            _, ok = kill_summary.summarise(kill_summary.read([root]), "nightly", "0x10", 100)
        self.assertFalse(ok)

    def test_a_shard_that_never_reported_is_noticed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_summary(root, "a", RESULT.format(first=0, last=499, runs=500, failures=0, ops=1))
            text, ok = kill_summary.summarise(kill_summary.read([root]), "exit-gate", "0x10", 1000)
        self.assertFalse(ok)
        self.assertIn("Only 500 of 1000", text)

    def test_nothing_found_is_a_failure(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            text, ok = kill_summary.summarise(kill_summary.read([Path(tmp)]), "nightly", "0x10", 0)
        self.assertFalse(ok)
        self.assertIn("No result line", text)

    def test_lines_that_are_not_results_are_ignored(self) -> None:
        self.assertIsNone(kill_summary.parse_line("ej-kill master_seed=0x1 iterations=5"))
        self.assertIsNone(kill_summary.parse_line("FAIL seed=0x1 iteration=3"))
        kind, values = kill_summary.parse_line(RESULT.format(first=0, last=1, runs=2, failures=0, ops=3)) or ("", {})
        self.assertEqual(kind, "random kills")
        self.assertEqual(values["runs"], "2")


if __name__ == "__main__":
    unittest.main()
