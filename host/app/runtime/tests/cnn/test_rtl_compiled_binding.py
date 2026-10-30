import importlib.util,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class CompiledBinding(unittest.TestCase):
 def module(self):
  p=ROOT/'scripts/cnn/freeze_rtl_artifacts.py';self.assertTrue(p.exists(),'Compiler-owned artifact binding missing')
  s=importlib.util.spec_from_file_location('compiled_binding',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
 def test_exact_compiler_owned_cache_only(self):
  m=self.module()
  with tempfile.TemporaryDirectory() as d:
   wd=Path(d).resolve();lib=wd/'xsim.dir/xil_defaultlib'
   self.assertEqual(m.unexpected_changes(wd,[lib/'short.sdb',lib/'arch_package.sdb',lib/'xil_defaultlib.rlx']),[])
   for changed in (lib/'VX_core.sdb',wd/'programs/id2.bin',wd/'cnn_host.c',wd.parent/'short.sdb'):
    self.assertTrue(m.unexpected_changes(wd,[changed]))
if __name__=='__main__':unittest.main()
