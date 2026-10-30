"""Mutation tests of actual linked RV32 bytes, never a substitute CPU ELF."""
import copy,json,pathlib,struct,sys,tempfile,unittest
from scripts.unified.audit_int16_layout import audit,elf_info,CODE,CRT
from scripts.s8.audit_rv32 import sections
BUILD=pathlib.Path(sys.argv[1]).resolve();sys.argv=sys.argv[:1]
class Layout(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.d=pathlib.Path(self.temp.name);self.elf=(BUILD/'kernel.elf').read_bytes();self.wire=(BUILD/'code.bin').read_bytes();self.meta=json.loads((BUILD/'receipt.json').read_text())
 def tearDown(self):self.temp.cleanup()
 def check(self,elf=None,wire=None,meta=None):
  e=self.d/'kernel.elf';w=self.d/'code.bin';e.write_bytes(self.elf if elf is None else elf);w.write_bytes(self.wire if wire is None else wire);return audit(e,w,self.meta if meta is None else meta)
 def ph(self):
  h=struct.unpack_from('<HHIIIIIHHHHHH',self.elf,16)
  return h,next(h[4]+i*32 for i in range(h[9]) if struct.unpack_from('<I',self.elf,h[4]+i*32)[0]==1 and struct.unpack_from('<I',self.elf,h[4]+i*32+8)[0]==CRT)
 def test_real_owned_layout(self):
  r=self.check();self.assertEqual(r['code_bytes'],65536);self.assertGreaterEqual(r['tls_base'],CRT+0x400);self.assertLessEqual(r['tls_end'],CRT+0xfc0);self.assertIsNone(r['gpu_raw_cycles'])
 def test_guard_changed(self):
  b=bytearray(self.wire);b[-1]^=1
  with self.assertRaises(ValueError):self.check(wire=b)
 def test_body_changed(self):
  b=bytearray(self.wire);b[4]^=1
  with self.assertRaises(ValueError):self.check(wire=b)
 def test_ps_header_has_extra_initialized_byte(self):
  h,p=self.ph();b=bytearray(self.elf);struct.pack_into('<I',b,p+16,h[4]+h[9]*32+1)
  with self.assertRaises(ValueError):self.check(elf=b)
 def test_ps_load_outside_crt(self):
  _,p=self.ph();b=bytearray(self.elf);struct.pack_into('<II',b,p+8,CRT+0x1000,CRT+0x1000)
  with self.assertRaises(ValueError):self.check(elf=b)
 def test_ps_executable(self):
  _,p=self.ph();b=bytearray(self.elf);struct.pack_into('<I',b,p+24,7)
  with self.assertRaises(ValueError):self.check(elf=b)
 def test_initialized_bss_disallowed(self):
  h=struct.unpack_from('<HHIIIIIHHHHHH',self.elf,16);raw,h2,_,_=elf_info(BUILD/'kernel.elf');named,_,_=sections(raw,h2);target=named['.bss'];b=bytearray(self.elf)
  index=next(i for i in range(h[11]) if struct.unpack_from('<10I',b,h[5]+i*40)==tuple(target));struct.pack_into('<I',b,h[5]+index*40+4,1);struct.pack_into('<I',b,h[5]+index*40+16,0)
  with self.assertRaises(ValueError):self.check(elf=b)
 def test_tls_binding_drift(self):
  m=copy.deepcopy(self.meta);m['runtime']['tls_base']=CODE
  with self.assertRaises(ValueError):self.check(meta=m)
 def test_dynamic_loader_segment_disallowed(self):
  _,p=self.ph();b=bytearray(self.elf);struct.pack_into('<I',b,p,2)
  with self.assertRaises(ValueError):self.check(elf=b)
if __name__=='__main__':unittest.main(verbosity=2)
