import importlib,unittest
class Cross(unittest.TestCase):
 def test_cross_requires_matching_arch_kernel_and_headers(self):
  cpu=importlib.import_module('scripts.s8.build_cpu');kernel=importlib.import_module('scripts.s8.build_kernel')
  with self.assertRaises(ValueError):cpu.validate_elf(b'host ELF')
  good=dict(release='5.4.0-xilinx-v2020.1',config='CONFIG_ARM64=y\nCONFIG_DEBUG_FS=y\nCONFIG_CRYPTO_SHA256=y\n',symvers=b'symbols',headers=True)
  self.assertTrue(kernel.validate_kernel(good))
  for key,value in [('release','6.1.0'),('config','CONFIG_X86=y'),('symvers',b''),('headers',False)]:
   bad=dict(good);bad[key]=value
   with self.assertRaises(ValueError):kernel.validate_kernel(bad)
  self.assertEqual(kernel.scope(None),'PRE_BINDING_KERNEL_COMPILE_NOT_IMAGE')
if __name__=='__main__':unittest.main()
