import unittest
from scripts.hunet_opt.catalog import compile_catalog,op_names
class Coverage(unittest.TestCase):
    def test_names_from_frozen_enum_and_all_stages(self):
        names=op_names();self.assertEqual(names[4],'HUNET_GN_STATS');self.assertEqual(names[15],'VXL_FUSED_OP')
        for candidate in ('G4','G4_C1','G4_C16'):
            c=compile_catalog(candidate);self.assertEqual(len(c['operator_coverage']),100)
            self.assertEqual((c['gpu_jobs'],c['cpu_jobs']),(103,13))
            self.assertEqual([r['stage'] for r in c['operator_coverage']],list(range(100)))
            self.assertEqual(c['scalar_policy']['gap_reorder'],False)
            self.assertTrue(all(row['original_ops'] for row in c['operator_coverage']))
if __name__=='__main__':unittest.main()
