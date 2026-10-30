"""Catch lost natural exits, merged streams and overwritten evidence."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'scripts/hunet_opt/run.py'
TEST_TEMP = ROOT / 'build/opt-test-temp'


class EvidenceRunnerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        TEST_TEMP.mkdir(parents=True, exist_ok=True)
        assert ROOT.resolve() in TEST_TEMP.resolve().parents

    def test_run_exit_and_no_overwrite(self):
        self.assertTrue(RUNNER.is_file(), 'missing evidence runner')
        spec = importlib.util.spec_from_file_location('opt_run', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as td:
            cwd = Path(td)
            out = cwd / 'evidence'
            rc = module.run([sys.executable, '-c',
                             'import sys; print("stdout-seven"); '
                             'print("stderr-seven", file=sys.stderr); sys.exit(7)'], cwd, out)
            self.assertEqual(rc, 7)
            self.assertEqual((out / 'stdout.log').read_text().strip(), 'stdout-seven')
            self.assertEqual((out / 'stderr.log').read_text().strip(), 'stderr-seven')
            receipt = json.loads((out / 'command.json').read_text())
            self.assertEqual(receipt['exit_code'], 7)
            before = {p.name: p.read_bytes() for p in out.iterdir()}
            with self.assertRaises(FileExistsError):
                module.run([sys.executable, '-c', 'print("overwrite")'], cwd, out)
            self.assertEqual(before, {p.name: p.read_bytes() for p in out.iterdir()})

    def test_cli_returns_child_exit(self):
        self.assertTrue(RUNNER.is_file(), 'missing evidence runner')
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as td:
            p = subprocess.run([sys.executable, str(RUNNER), '--task', 'T01',
                                '--out', str(Path(td) / 'cli'), '--',
                                sys.executable, '-c', 'raise SystemExit(17)'], cwd=str(ROOT))
            self.assertEqual(p.returncode, 17)


if __name__ == '__main__':
    unittest.main()
