"""Controller coverage declarations, never an application or GPU result."""
import unittest
from scripts.unified.run_software import matrix,FOUNDATION
class SoftwareMatrix(unittest.TestCase):
 def test_all_native_and_numeric_groups_are_declared(self):
  m,_=matrix();self.assertEqual(len(m['groups']),14);self.assertIsNone(m['gpu_raw_cycles']);self.assertEqual(m['board'],'NOT_RUN')
 def test_real_concurrency_not_only_synthetic_scheduler(self):
  m,_=matrix();thread={g['id'] for g in m['groups'] if 'thread' in g['modes']};self.assertTrue({'s8-joint','integer-extension','p8-public','native-media','installed','stage','display'}<=thread)
 def test_original_retirement_and_integer_legacy_gates_kept(self):
  self.assertTrue({'test-v2-execution','test-v2-close','test-v2-batch','test-unified-extension-plan','test-unified-gelu-token'}<=set(FOUNDATION));self.assertEqual(len(FOUNDATION),len(set(FOUNDATION)))
 def test_image_background_bytes_run_in_each_host_mode(self):
  self.assertIn('test-unified-image-fill',FOUNDATION)
if __name__=='__main__':unittest.main()
