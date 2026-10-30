import unittest
from scripts.hunet_opt.summary import eligible_for_combine
from scripts.hunet_opt.catalog import compile_catalog,op_names
from scripts.hunet_opt.policy import build_policy
class Candidates(unittest.TestCase):
    def test_only_bounded_green_receipts_can_combine(self):
        good=dict(candidate='G1',software='PASS',source_sha256='1'*64,catalog_sha256='2'*64,r2='PASS',runtime_finite=True,scope='HOST_NUMERIC_MODEL',gpu_cycles=None)
        self.assertEqual(eligible_for_combine([good]),['G1'])
        for change in [dict(software='FAIL'),dict(r2='FAIL'),dict(runtime_finite=False),dict(source_sha256=''),dict(gpu_cycles=686600903),dict(combinable=False)]:
            self.assertEqual(eligible_for_combine([dict(good,**change)]),[])
        with self.assertRaises(ValueError):eligible_for_combine([good,good])
    def test_combined_is_explicit_not_fastest_claim(self):
        c=compile_catalog('COMBINED');p=build_policy('COMBINED')
        self.assertEqual((c['gpu_jobs'],c['cpu_jobs'],c['logical_jobs']),(71,13,1023))
        self.assertEqual(p['tile_n'],2);self.assertEqual(p['scalar_chunk'],4)
        self.assertTrue(p['parallel_gn'] and p['views'] and p['pack'] and p['locate'] and p['load_fastwait'])
        self.assertFalse(p['internal_timing']);self.assertTrue(build_policy('G2_TIMING')['internal_timing'])
        self.assertEqual(op_names()[4],'HUNET_GN_STATS');self.assertEqual(op_names()[15],'VXL_FUSED_OP')
if __name__=='__main__':unittest.main()
