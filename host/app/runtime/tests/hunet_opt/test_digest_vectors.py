"""Standard library SHA is independent of the backend under test."""
import hashlib
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT/'build/test_opt_digest'


class DigestVectors(unittest.TestCase):
    def test_standard_boundary_and_incremental_vectors(self):
        for n in [0, 1, 55, 56, 63, 64, 65, 4095, 4096, 4097]:
            with self.subTest(n=n):
                actual = subprocess.check_output([str(BIN), str(n)]).decode().strip()
                self.assertEqual(actual, hashlib.sha256(bytes(i % 256 for i in range(n))).hexdigest())

    def test_failures_leave_output_and_release_context(self):
        output = subprocess.check_output([str(BIN)]).decode()
        self.assertIn('NO_FAKE_OUTPUT_NO_LEAK', output)


if __name__ == '__main__':
    unittest.main()
