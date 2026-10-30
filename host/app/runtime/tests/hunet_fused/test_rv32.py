"""Reopen actual build outputs; reject corruption independently of build exit."""
import json,pathlib,sys,tempfile,unittest,shutil
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_fused'))
from audit_rv32 import audit,audit_program
class RV32(unittest.TestCase):
 def test_actual_elf_and_corrupt_wire(self):
  latest=ROOT/'build/hunet-fused-rv32/LATEST'
  self.assertTrue(latest.is_file(),'ACTUAL_FUSED_BUILD_REQUIRED')
  p=pathlib.Path(latest.read_text().strip());r=audit(p)
  self.assertEqual(r['program_id'],9);self.assertLess(r['wire_bytes'],8*1024*1024)
  temp=ROOT/'build/test-tmp';temp.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=temp) as directory:
   bad=pathlib.Path(directory)/'code.bin';raw=bytearray((p/'code.bin').read_bytes());raw[0]^=1;bad.write_bytes(raw)
   with self.assertRaisesRegex(ValueError,'WIRE_SEGMENT_CONTENT'):
    audit_program(p/'kernel.elf',bad,dict(program_id=9))
if __name__=='__main__':unittest.main()
