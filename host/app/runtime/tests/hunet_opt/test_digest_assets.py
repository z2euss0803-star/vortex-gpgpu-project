"""Asset SHA equivalence only: no model, GPU or board execution."""
import hashlib
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
VAULT = ROOT.parents[1]
BIN = ROOT/'build/test_opt_digest'


class DigestAssets(unittest.TestCase):
    def test_frozen_model_weights_and_thirteen_layer_pairs(self):
        contract = json.loads((ROOT/'contracts/hunet_v5.json').read_text(encoding='utf-8'))
        records = {x['path']: x for x in contract['files'] if x['role'] in ('onnx', 'weights')}
        captures = json.loads((VAULT/'opt-fixtures/gelu-captures.json').read_text())['files']
        tensors = {p: r for p, r in captures.items() if p.endswith('.f32')}
        self.assertEqual(len(tensors), 26, 'all 13 input/reference pairs must be present')
        records.update(tensors)
        self.assertEqual(len(records), 28)
        for name, receipt in sorted(records.items()):
            with self.subTest(asset=name):
                path = VAULT/name
                self.assertEqual(path.stat().st_size, receipt['bytes'])
                h = hashlib.sha256()
                with path.open('rb') as f:
                    for block in iter(lambda: f.read(1024*1024), b''):
                        h.update(block)
                self.assertEqual(h.hexdigest(), receipt['sha256'])
                actual = subprocess.check_output([str(BIN), '--file', str(path)]).decode().strip()
                self.assertEqual(actual, h.hexdigest())
        print('HOST_ASSET_SHA_EQUIVALENCE_28_FILES_NOT_INFERENCE_NOT_BOARD')


if __name__ == '__main__':
    unittest.main()
