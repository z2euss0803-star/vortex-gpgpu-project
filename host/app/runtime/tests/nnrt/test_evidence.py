import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from scripts.nnrt.run_step import run_step
from scripts.nnrt.verify_candidate import verify_candidate


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="nnrt-", dir=str(Path(__file__).resolve().parents[2] / "build"))
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "source.c").write_bytes(b"original\n")
        self.frozen = {"files": [{"path": "source.c", "bytes": 9, "sha256": hashlib.sha256(b"original\n").hexdigest()}]}

    def identity(self):
        return {"scope": "HOST_PROTOCOL_TEST", "source_manifest": self.frozen, "gpu_raw_cycles": None}

    def test_nonzero_exit_is_retained(self):
        out = self.root / "run"
        rc = run_step([sys.executable, "-c", "import os,sys;os.write(1,b'raw\\x00out');os.write(2,b'original error');sys.exit(7)"], self.root, out, self.identity())
        self.assertEqual(rc, 7)
        self.assertEqual((out / "stdout.log").read_bytes(), b"raw\x00out")
        self.assertEqual((out / "stderr.log").read_bytes(), b"original error")
        self.assertEqual(json.loads((out / "exit.json").read_text())["natural_exit"], 7)
        self.assertIsNone(json.loads((out / "metrics.json").read_text())["gpu_raw_cycles"])
        for line in (out / "events.jsonl").read_text().splitlines():
            self.assertEqual(json.loads(line)["natural_exit"], 7)
        self.assertTrue((out / "failure.json").exists())

    def test_existing_run_refused(self):
        out = self.root / "run"
        out.mkdir()
        (out / "original").write_bytes(b"keep")
        with self.assertRaises(FileExistsError):
            run_step([sys.executable, "-c", "pass"], self.root, out, self.identity())
        self.assertEqual((out / "original").read_bytes(), b"keep")
        self.assertEqual(len(list(out.iterdir())), 1)

    def test_source_drift_rejected(self):
        (self.root / "source.c").write_bytes(b"Original\n")
        self.assertFalse(verify_candidate(self.root, self.frozen)["admitted"])
        with self.assertRaises(ValueError):
            run_step([sys.executable, "-c", "pass"], self.root, self.root / "run", self.identity())

    def test_valid_source_admitted(self):
        self.assertTrue(verify_candidate(self.root, self.frozen)["admitted"])

    def test_path_escape_rejected(self):
        self.frozen["files"][0]["path"] = "../outside"
        self.assertFalse(verify_candidate(self.root, self.frozen)["admitted"])

    def test_empty_manifest_rejected(self):
        self.assertFalse(verify_candidate(self.root, {"files": []})["admitted"])

    def test_success_receipt_and_environment_filter(self):
        out = self.root / "run"
        os.environ["NNRT_PRIVATE_TEST_TOKEN"] = "do-not-copy"
        self.addCleanup(os.environ.pop, "NNRT_PRIVATE_TEST_TOKEN", None)
        rc = run_step([sys.executable, "-c", "import os;assert 'NNRT_PRIVATE_TEST_TOKEN' not in os.environ"], self.root, out, self.identity())
        self.assertEqual(rc, 0)
        self.assertTrue((out / "result_manifest.json").exists())
        self.assertFalse((out / "failure.json").exists())


if __name__ == "__main__":
    unittest.main()
