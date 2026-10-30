import json
from pathlib import Path
import unittest
from scripts.nnrt.mosaic_workload import workload,admit


class WorkloadTests(unittest.TestCase):
    def test_complete_graph_has_bounded_evidence_admission(self):
        model=json.loads((Path(__file__).resolve().parents[2]/'config/mosaic/model-v1.json').read_text())
        old=workload(model);split=workload(model,limits=(4096,4,4096,4096,4096,4096))
        self.assertEqual(len(split['phases']),77)
        self.assertEqual(split['phases'][1]['items'],614400)
        self.assertGreater(split['jobs'],old['jobs'])
        self.assertEqual(split['raw_evidence_bytes'],split['jobs']*512)
        self.assertIsNone(split['gpu_raw_cycles'])
        with self.assertRaisesRegex(ValueError,'WORKLOAD_BUDGET_EXCEEDED'):admit(split,8,4096)
        self.assertIs(admit(split,split['jobs'],split['raw_evidence_bytes']),split)


if __name__=='__main__':unittest.main()
