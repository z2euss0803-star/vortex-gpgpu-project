"""Captured values exercise math only; these are GELU inputs, not GN input captures."""
import hashlib
import json
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[2];VAULT=ROOT.parents[1]
class Captures(unittest.TestCase):
    def test_real_available_values(self):
        records=json.loads((VAULT/'opt-fixtures/gelu-captures.json').read_text())['files']
        inputs={p:v for p,v in records.items() if p.endswith('input.f32')}
        self.assertEqual(len(inputs),13)
        for name,record in sorted(inputs.items()):
            p=VAULT/name
            self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),record['sha256'])
            subprocess.run([str(ROOT/'build/test_opt_gn_math'),str(p)],check=True)
        print('ORIGINAL_GN_INPUT_LAYER_COMPARISON_NOT_RUN; REAL_GELU_VALUES_MATH_ONLY')
if __name__=='__main__':unittest.main()
