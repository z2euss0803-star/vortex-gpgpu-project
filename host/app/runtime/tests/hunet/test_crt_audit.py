"""Catch unsafe low-PS exit writes, clobbered return and incorrect TLS/stack bounds."""
import importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2];VAULT=ROOT.parents[1]
class CRTTests(unittest.TestCase):
 def auditor(self):
  p=ROOT/'scripts/hunet/audit_program.py';self.assertTrue(p.is_file(),'program auditor missing')
  s=importlib.util.spec_from_file_location('audit_hunet_program',str(p));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
 def test_low_io_crt_rejected(self):
  a=self.auditor();old=VAULT/'work/hunet_swopt_20261011_v5'
  with self.assertRaisesRegex(ValueError,'LOW_PS_IO'):
   a.audit_program(old/'build/rv32_exact/kernel.elf',old/'delivery/hunet_swopt_20261011_v5/code.bin',dict(program_id=6))
 def test_real_return_preserved(self):
  a=self.auditor();d=ROOT/'build/hunet-rv32';self.assertTrue((d/'kernel.elf').is_file(),'Linux CRT build missing')
  r=a.audit_program(d/'kernel.elf',d/'code.bin',dict(program_id=6))
  self.assertEqual(r['exit_register'],8);self.assertTrue(r['return_saved_before_perf'])
  self.assertEqual(r['exit_slots'],[0x10002088,0x10002188,0x10002288,0x10002388])
 def test_stack_tls_bounds(self):
  a=self.auditor();d=ROOT/'build/hunet-rv32';self.assertTrue((d/'kernel.elf').is_file(),'Linux CRT build missing')
  r=a.audit_program(d/'kernel.elf',d/'code.bin',dict(program_id=6))
  self.assertEqual(r['entry'],0x80000000);self.assertEqual(r['stack_active_base'],0xfff70000)
  self.assertEqual(r['stack_reserved_base'],0xfff00000);self.assertLessEqual(r['tls_end'],0x80800000)
  self.assertEqual(r['wire_bytes']%64,0)
 def test_wire_tamper_rejected(self):
  a=self.auditor();d=ROOT/'build/hunet-rv32';self.assertTrue((d/'code.bin').is_file(),'Linux CRT build missing')
  (ROOT/'build/test-tmp').mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(ROOT/'build/test-tmp')) as tmp:
   bad=pathlib.Path(tmp)/'bad.bin';data=bytearray((d/'code.bin').read_bytes());data[0]^=1;bad.write_bytes(data)
   with self.assertRaisesRegex(ValueError,'WIRE_SEGMENT'):
    a.audit_program(d/'kernel.elf',bad,dict(program_id=6))
if __name__=='__main__':unittest.main()
