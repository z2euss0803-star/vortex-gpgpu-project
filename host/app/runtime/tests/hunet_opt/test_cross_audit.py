import unittest,struct,tempfile,pathlib,hashlib
from scripts.hunet_opt.cross import elf_machine,audit_sha2_isolation
from scripts.hunet_opt.build_rv32 import check_defines,stack_audit,stale_sdk_members,required_device_symbols,clang_resource_dir,SDK_SOURCES
from scripts.hunet_opt.build_identity import verify_snapshot
from scripts.hunet_opt.build_rv32 import audit_instruction_words,static_frame
class Cross(unittest.TestCase):
 def test_actual_instruction_masks_and_dynamic_stack_refusal(self):
  self.assertEqual(audit_instruction_words([0x0400000b,0x0000400b,0x0ff0000f,0x0ff0000f])['wmma'],1)
  with self.assertRaises(ValueError):audit_instruction_words([0x0400000b])
  self.assertEqual(static_frame([((4096-32)<<20)|(2<<15)|(2<<7)|0x13]),32)
  with self.assertRaises(ValueError):static_frame([(0x20<<25)|(10<<20)|(2<<15)|(2<<7)|0x33])
 def test_entries_follow_candidate_policy(self):
  from scripts.hunet_opt.policy import build_policy
  self.assertNotIn('opt_grouped_gemm_body',required_device_symbols(build_policy('G1')))
  self.assertIn('opt_grouped_gemm_body',required_device_symbols(build_policy('G2_PACK')))
 def test_loader_cannot_guess_clang_resource_dir(self):
  with self.assertRaises(ValueError):clang_resource_dir(pathlib.Path('/missing/compiler/tree'))
 def test_cross_identity(self):
  b=bytearray(64);b[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',b,18,183)
  self.assertEqual(elf_machine(bytes(b),2,183),'PASS')
  struct.pack_into('<H',b,18,62)
  with self.assertRaises(ValueError):elf_machine(bytes(b),2,183)
  with self.assertRaises(ValueError):check_defines(['-DNUM_CORES=8'])
  with self.assertRaises(ValueError):audit_sha2_isolation({'base':'sha256h q0,q1,v2.4s','sha2':'sha256h q0,q1,v2.4s'})
  self.assertEqual(audit_sha2_isolation({'base':'add x0,x0,1','sha2':'sha256h q0,q1,v2.4s'})['integrity'],'PASS')
 def test_stack_bounds_not_only_single_frame(self):
  r=stack_audit({'main':100,'f':400,'g':800},{'main':['f'],'f':['g'],'g':[]},['main'])
  self.assertEqual(r['max_chain_bytes'],1300)
  with self.assertRaises(ValueError):stack_audit({'main':4000,'f':5000},{'main':['f'],'f':[]},['main'])
  with self.assertRaises(ValueError):stack_audit({'main':1},{'main':['main']},['main'])
 def test_no_stale_scheduler_archive_member(self):
  self.assertIn('vx_print.c',SDK_SOURCES);self.assertIn('vx_spawn.c',SDK_SOURCES)
  self.assertEqual(stale_sdk_members(['vx_spawn.o','vx_spawn.c.o','vx_start.S.o','vx_perf.o','tinyprintf.o']),['vx_spawn.o','vx_spawn.c.o','vx_start.S.o','vx_perf.o'])
 def test_snapshot_cannot_relabel_an_old_binary(self):
  tmp=pathlib.Path(__file__).resolve().parents[2]/'build/test-tmp';tmp.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(tmp)) as d:
   root=pathlib.Path(d);p=root/'a.c';p.write_bytes(b'int a;\n');b=p.read_bytes()
   i=dict(schema=1,source_commit='a'*40,files={'a.c':dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),git_blob=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest())})
   self.assertEqual(verify_snapshot(root,i)['source_commit'],'a'*40)
   p.write_bytes(b'int b;\n')
   with self.assertRaises(ValueError):verify_snapshot(root,i)
if __name__=='__main__':unittest.main()
