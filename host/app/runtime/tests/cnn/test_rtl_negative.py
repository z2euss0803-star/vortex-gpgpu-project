import importlib.util,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/cnn'))
from generate_kernels import generate_program
class NegativeRTL(unittest.TestCase):
 def module(self):
  p=ROOT/'scripts/cnn/negative_rtl.py';self.assertTrue(p.is_file(),'Actual bad-tail RTL runner missing')
  s=importlib.util.spec_from_file_location('negative_rtl',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
 def test_mutation_changes_only_tail_limit(self):
  m=self.module();good=generate_program(2);bad=m.bad_tail(good)
  self.assertEqual(len(bad),4096)
  differences=[i for i in range(0,len(good),4) if good[i:i+4]!=bad[i:i+4]]
  self.assertEqual(differences,[292])
  self.assertEqual(int.from_bytes(bad[292:296],'little'),0x0406bb93)
  with self.assertRaises(ValueError):m.bad_tail(bytes(4096))
 def test_requires_positive_receipt(self):
  m=self.module()
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)
   with self.assertRaises(ValueError):m.prepare(p,p/'negative')
   self.assertFalse((p/'negative').exists())
if __name__=='__main__':unittest.main()
