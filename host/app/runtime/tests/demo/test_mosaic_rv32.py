import unittest,copy
from scripts.demo.mosaic_rv32_contract import plan_runtime,validate_runtime
class RuntimeContract(unittest.TestCase):
 def test_sidecar_avoids_crt_args_and_weights(self):
  p=plan_runtime(262144,128,4096)
  self.assertEqual(p['rodata_gpu'],0x80110000);self.assertEqual(p['rodata_bytes'],6307840)
  self.assertLess(p['runtime_bytes'],0x100000)
  self.assertLessEqual(p['rodata_gpu']+p['rodata_bytes'],0x80800000)
  self.assertEqual(validate_runtime(p),p)
 def test_crt_low_ps_or_unsafe_stack_rejected(self):
  p=plan_runtime(65536,128,4096)
  for k,v in [('crt_base',0),('stack_stride',4096),('fcsr_init',1),('rodata_gpu',0x80100040),('rodata_bytes',6307844),('entry',0x400000)]:
   bad=copy.deepcopy(p);bad[k]=v
   with self.assertRaises(ValueError):validate_runtime(bad)
 def test_large_tls_or_stack_rejected(self):
  for args in [(0x100000,128,1024),(65536,16384,1024),(65536,128,8192)]:
   with self.assertRaises(ValueError):plan_runtime(*args)
if __name__=='__main__':unittest.main()
