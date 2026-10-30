import copy,importlib,unittest
class Summary(unittest.TestCase):
 def test_mixed_commit_wait_image_and_missing_retest_never_all_pass(self):
  api=importlib.import_module('scripts.s8.summary');r=dict(layer='SW',state='PASS',source_sha256='a'*64,scope='UBUNTU_SOFTWARE_ONLY',exit=0,retest_required=True)
  with self.assertRaises(ValueError):api.summarize([r])
  self.assertFalse(api.summarize([dict(r,retest_done=True)])['all_pass'])
 def test_field_entry_requires_layer_states_and_no_autorun(self):
  from scripts.s8.field_entry import validate,render
  from scripts.s8.summary import summarize
  state=summarize([]);text=render(state)
  self.assertTrue(validate(text,state));self.assertIn('203',text)
  for bad in (text.replace('OPEN_NOT_PASS','CLOSED'),text.replace('BOARD_S8','missing'),text+'\n dd if=image.ub of=/dev/sdb\n'):
   with self.assertRaises(ValueError):validate(bad,state)
 def test_separate_layers_and_missing_evidence_never_all_pass(self):
  api=importlib.import_module('scripts.s8.summary')
  sw=dict(layer='SW',state='PASS',source_sha256='a'*64,scope='UBUNTU_SOFTWARE_ONLY',exit=0)
  result=api.summarize([sw]);self.assertEqual(result['states']['SW'],'PASS');self.assertEqual(result['states']['BOARD_S8'],'NOT_RUN');self.assertIsNone(result['gpu_raw_cycles']);self.assertFalse(result['all_pass'])
  for bad in [dict(sw,exit=1),dict(sw,scope='REAL_BOARD'),dict(sw,source_sha256='short')]:
   with self.assertRaises(ValueError):api.summarize([bad])
  with self.assertRaises(ValueError):api.summarize([sw,dict(sw,layer='CROSS_DEVICE',source_sha256='b'*64)])
 def test_wait_image_and_known_failures_remain(self):
  api=importlib.import_module('scripts.s8.summary');r=dict(layer='IMAGE',state='WAIT_HW_EXPORT',source_sha256='a'*64,scope='IMAGE',exit=2)
  out=api.summarize([r]);self.assertEqual(out['states']['IMAGE'],'WAIT_HW_EXPORT');self.assertFalse(out['all_pass'])
if __name__=='__main__':unittest.main()
