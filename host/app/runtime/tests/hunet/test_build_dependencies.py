"""Build protocol fixtures must recompile when the shared ABI changes."""
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class BuildDependencyTests(unittest.TestCase):
    def test_strict_backend_tracks_engine_abi(self):
        result = subprocess.run(
            ['make', '-n', '-W', 'include/vxl/engine.h',
             'build/tests/support/strict_backend.o'],
            cwd=ROOT, text=True, capture_output=True, check=True)
        self.assertIn('-c tests/support/strict_backend.c', result.stdout)


if __name__ == '__main__':
    unittest.main()
