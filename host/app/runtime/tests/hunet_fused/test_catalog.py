"""Catch skipped spans, mixed stages and changed frozen matrix links."""
import copy
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE = ROOT / 'scripts/hunet_fused/catalog.py'


class FusionCatalog(unittest.TestCase):
    def setUp(self):
        self.assertTrue(MODULE.is_file(), 'fusion catalog module not implemented')
        spec = importlib.util.spec_from_file_location('fusion_catalog', str(MODULE))
        self.catalog = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.catalog)
        self.contract = self.catalog.freeze_catalog(ROOT)

    def test_exact_coverage(self):
        result = self.catalog.compile_schedule(self.contract)
        self.assertEqual((result['logical_jobs'], result['triplets'],
                          result['matrix_stages'], len(result['gpu']), len(result['cpu'])),
                         (1023, 314, 35, 103, 13))
        covered = []
        for item in result['logical_ranges']:
            covered.extend(range(item['first'], item['last'] + 1))
        self.assertEqual(covered, list(range(1, 1024)))
        self.assertEqual(result['gpu'][0]['first'], 1)
        self.assertEqual(result['gpu'][0]['last'], 96)
        self.assertEqual(len(result['gpu'][0]['descriptors']), 32)
        self.assertEqual([x['sequence'] for x in result['cpu']],
                         [296, 591, 768, 847, 896, 913, 930,
                          947, 964, 981, 1000, 1011, 1020])

    def test_bad_link_rejected(self):
        for job, word in [(0, 13), (1, 8), (1, 13), (2, 8), (1, 15)]:
            changed = copy.deepcopy(self.contract)
            changed['jobs'][job][word] ^= 64
            with self.subTest(job=job, word=word):
                with self.assertRaises(ValueError):
                    self.catalog.compile_schedule(changed)

    def test_mixed_stage_or_gap_rejected(self):
        for job, word, value in [(1, 30, 1), (0, 4, 2), (96, 4, 96)]:
            changed = copy.deepcopy(self.contract)
            changed['jobs'][job][word] = value
            with self.subTest(job=job, word=word):
                with self.assertRaises(ValueError):
                    self.catalog.compile_schedule(changed)

    def test_frozen_model_weight_or_cpu_role_rejected(self):
        for role in ('weights', 'onnx', 'bit', 'xsa'):
            changed = copy.deepcopy(self.contract)
            changed['baseline']['identities'][role] = '00' * 32
            with self.subTest(role=role):
                with self.assertRaises(ValueError):
                    self.catalog.compile_schedule(changed)
        changed = copy.deepcopy(self.contract)
        changed['jobs'][295][3] = 14
        with self.assertRaises(ValueError):
            self.catalog.compile_schedule(changed)


if __name__ == '__main__':
    unittest.main()
