import importlib.util,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/cnn'))
from generate_kernels import generate_program
class CompletedRTL(unittest.TestCase):
 def test_canonical_programs_not_just_mutually_matching_hashes(self):
  path=ROOT/'scripts/cnn/audit_completed_rtl.py';self.assertTrue(path.exists(),'Completed RTL auditor missing')
  s=importlib.util.spec_from_file_location('audit_completed_rtl',path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
  good={i:generate_program(i) for i in (2,3,4,5)};self.assertEqual(m.program_errors(good),[])
  for key in (2,3,4,5):
   bad=dict(good);bad[key]=bytes(4096);self.assertTrue(m.program_errors(bad))
  self.assertTrue(m.program_errors({2:good[2]}))
if __name__=='__main__':unittest.main()
