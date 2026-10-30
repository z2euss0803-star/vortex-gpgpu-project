"""Real historical ELF fixtures plus semantic mutations; no target execution."""
import pathlib,hashlib,json,struct,tempfile,unittest
from scripts.unified.audit_cross import inspect_elf,audit_production_symbols
ROOT=pathlib.Path(__file__).resolve().parent/'fixtures/cross'
class CrossAudit(unittest.TestCase):
 def fixture(self,target):
  meta=json.loads((ROOT/'manifest.json').read_text());row=next(x for x in meta['files'] if x['path']==target+'.elf');data=(ROOT/row['path']).read_bytes();self.assertEqual(hashlib.sha256(data).hexdigest(),row['sha256']);return data,row
 def check_mutation(self,target,change):
  data,row=self.fixture(target);data=bytearray(data);change(data)
  base=(pathlib.Path.cwd()/'build/U22-audit-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as directory:
   self.assertEqual(pathlib.Path(directory).resolve().parent,base)
   path=pathlib.Path(directory)/'changed.elf';path.write_bytes(data)
   with self.assertRaises(ValueError):inspect_elf(path,target,hashlib.sha256(data).hexdigest())
 def test_actual_aarch64_fixture_is_structurally_inspected_not_executed(self):
  _,row=self.fixture('aarch64');result=inspect_elf(ROOT/row['path'],'aarch64',row['sha256']);self.assertEqual(result['machine'],183);self.assertEqual(result['class_bits'],64);self.assertEqual(result['interpreter'],'/lib/ld-linux-aarch64.so.1');self.assertTrue(result['needed']);self.assertIsNone(result['gpu_raw_cycles']);self.assertFalse(result['target_executed']);audit_production_symbols(result)
 def test_actual_rv32_fixture_is_structurally_inspected_not_executed(self):
  _,row=self.fixture('rv32');result=inspect_elf(ROOT/row['path'],'rv32',row['sha256']);self.assertEqual(result['machine'],243);self.assertEqual(result['class_bits'],32);self.assertEqual(result['entry'],0x80000000);self.assertIsNone(result['interpreter']);self.assertFalse(result['needed']);self.assertIsNone(result['gpu_raw_cycles'])
 def test_digest_mismatch_does_not_become_a_target_compile_pass(self):
  _,row=self.fixture('aarch64')
  with self.assertRaises(ValueError):inspect_elf(ROOT/row['path'],'aarch64','0'*64)
 def test_host_machine_relabel_is_rejected_even_with_rehashed_bytes(self):
  self.check_mutation('aarch64',lambda b:struct.pack_into('<H',b,18,62))
 def test_big_endian_relabel_is_rejected_even_with_rehashed_bytes(self):
  self.check_mutation('rv32',lambda b:b.__setitem__(5,2))
 def test_outside_entry_is_rejected(self):
  self.check_mutation('aarch64',lambda b:struct.pack_into('<Q',b,24,0xdead00000000))
 def test_truncated_program_or_section_tables_are_rejected(self):
  self.check_mutation('rv32',lambda b:b.__delitem__(slice(len(b)-100,None)))
 def test_dynamic_runpath_is_rejected_with_no_host_path_assumptions(self):
  def change(b):
   phoff=struct.unpack_from('<Q',b,32)[0];size,count=struct.unpack_from('<HH',b,54)
   for i in range(count):
    ph=phoff+i*size
    if struct.unpack_from('<I',b,ph)[0]!=2:continue
    offset=struct.unpack_from('<Q',b,ph+8)[0];length=struct.unpack_from('<Q',b,ph+32)[0]
    for j in range(offset,offset+length,16):
     if struct.unpack_from('<q',b,j)[0]==0:struct.pack_into('<qQ',b,j,29,1);return
   self.fail('historical AArch64 ELF must contain the actual dynamic table')
  self.check_mutation('aarch64',change)
 def test_baremetal_undefined_symbol_is_rejected_after_rehash(self):
  def change(b):
   old=b'__libc_start_main\0';new=b'XTime_GetTime\0';self.assertIn(old,b);b[:]=b.replace(old,new+bytes(len(old)-len(new)))
  self.check_mutation('aarch64',change)
 def test_rv32_undefined_target_symbol_is_rejected_after_rehash(self):
  def change(b):
   shoff=struct.unpack_from('<I',b,32)[0];size,count=struct.unpack_from('<HH',b,46)
   for i in range(count):
    sh=shoff+i*size
    if struct.unpack_from('<I',b,sh+4)[0]!=2:continue
    offset,length=struct.unpack_from('<II',b,sh+16)
    for j in range(offset,offset+length,16):
     if struct.unpack_from('<I',b,j)[0] and b[j+12]&15==2 and struct.unpack_from('<H',b,j+14)[0] not in (0,0xfff1):struct.pack_into('<H',b,j+14,0);return
   self.fail('actual RV32 fixture must contain defined function symbols')
  self.check_mutation('rv32',change)
 def test_defined_device_test_bridge_is_not_accepted_as_production(self):
  data,row=self.fixture('aarch64');b=bytearray(data);shoff=struct.unpack_from('<Q',b,40)[0];size,count=struct.unpack_from('<HH',b,58);changed=False
  for i in range(count):
   sh=shoff+i*size
   if struct.unpack_from('<I',b,sh+4)[0]!=2:continue
   offset,length=struct.unpack_from('<QQ',b,sh+24);link=struct.unpack_from('<I',b,sh+40)[0];strings=struct.unpack_from('<Q',b,shoff+link*size+24)[0]
   for j in range(offset,offset+length,24):
    index=struct.unpack_from('<I',b,j)[0];start=strings+index;end=b.find(b'\0',start)
    if index and b[j+4]&15==2 and struct.unpack_from('<H',b,j+6)[0] not in (0,0xfff1) and end-start>=len(b's8_owner_bridge_create'):
     replacement=b's8_owner_bridge_create';b[start:end]=replacement+bytes(end-start-len(replacement));changed=True;break
   if changed:break
  self.assertTrue(changed,'actual historical ELF contains defined C++ function names')
  base=(pathlib.Path.cwd()/'build/U22-audit-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   directory=pathlib.Path(name).resolve();self.assertEqual(directory.parent,base);path=directory/'test-bridge.elf';path.write_bytes(b);inspection=inspect_elf(path,'aarch64',hashlib.sha256(b).hexdigest());self.assertIn('s8_owner_bridge_create',inspection['test_bridge_symbols'])
   with self.assertRaises(ValueError):audit_production_symbols(inspection)
if __name__=='__main__':unittest.main()
