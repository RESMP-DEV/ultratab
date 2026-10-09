"""Ultra Tab's iOS integration synchronization: poll the answer log."""

import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
import sys  # noqa: E402

sys.path.insert(0, str(ROOT / "scripts"))
import check_ultratab_ios  # noqa: E402


class WaitAnswersTest(unittest.TestCase):
    def test_polls_until_a_complete_row_arrives(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "answers.jsonl"

            def append_after_two_polls():
                calls = 0

                def exists(_path):
                    nonlocal calls
                    calls += 1
                    if calls >= 3:
                        log.write_text('{"how": "skipped"}\n')
                    return calls >= 3

                return exists

            with patch.object(Path, "exists", append_after_two_polls()):
                rows = check_ultratab_ios.wait_for_answers(
                    log, 1, timeout=2, interval=0.01
                )
            self.assertEqual(rows, [{"how": "skipped"}])

    def test_returns_what_arrived_when_the_deadline_passes(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "answers.jsonl"
            log.write_text('{"how": "accepted"}\nbroken\n')
            started = time.monotonic()
            rows = check_ultratab_ios.wait_for_answers(
                log, 2, timeout=0.02, interval=0.01
            )
            self.assertEqual(rows, [])
            self.assertLess(time.monotonic() - started, 1)


if __name__ == "__main__":
    unittest.main()
