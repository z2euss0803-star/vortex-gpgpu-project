import copy,hashlib,importlib.util,pathlib,struct,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class CrossTests(unittest.TestCase):
 def test_failed_post_build_receipts_cannot_be_reused(self):
  s=importlib.util.spec_from_file_location('fused_cross_failed_receipt',ROOT/'scripts/hunet_fused/cross.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
  # Minimal protocol fixtures only; these bytes are never build evidence.
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',elf,18,183)
   (root/'objects').mkdir()
   compiler=dict(target='aarch64-xilinx-linux',version='GCC 9.2.0')
   cpu=dict(scope='FUSED_CPU_AARCH64_BUILD',exit=0,compiler=compiler,elfs={},audit=dict(integrity='PASS'))
   for name in ('vxl_hunet_fused','libvortex-fused.so'):
    (root/'objects'/name).write_bytes(elf);cpu['elfs'][name]=dict(sha256=hashlib.sha256(elf).hexdigest(),machine='AArch64')
   (root/'cli.map').write_text('fixture');(root/'build.log.exit').write_text('0\n')
   m.checked_cpu(root,cpu)
   bad=copy.deepcopy(cpu);bad.update(error='SOURCE_IDENTITY:changed',audit=dict(integrity='FAIL'))
   with self.assertRaises(ValueError):m.checked_cpu(root,bad)
   bad=copy.deepcopy(cpu);bad['audit']['integrity']='FAIL'
   with self.assertRaises(ValueError):m.checked_cpu(root,bad)
   driver=root/'source/driver';driver.mkdir(parents=True);binding=bytes(range(256))*2
   (driver/'vxl_linux.ko').write_bytes(elf+binding)
   names=['shared_hunet_fused_'+n+'.o' for n in ('validate','state','evidence','program')]
   for n in names:(driver/n).write_bytes(b'fixture')
   kernel=dict(scope='FUSED_KERNEL_AARCH64_BUILD',exit=0,W=1,Werror=True,kernel_release='5.4.0',module_sha256=m.sha(driver/'vxl_linux.ko'),fused_objects=names,fused_symbols=['vxl_fused_init','vxl_fused_check_program','vxl_fused_gpu_completed','vxl_fused_owner_trace'],audit=dict(integrity='PASS'))
   m.audit_cross_files(root,kernel,binding)
   bad=copy.deepcopy(kernel);bad.update(error='KERNEL_INPUT_CHANGED',audit=dict(integrity='FAIL'))
   with self.assertRaises(ValueError):m.audit_cross_files(root,bad,binding)
   bad=copy.deepcopy(kernel);bad['audit']['integrity']='FAIL'
   with self.assertRaises(ValueError):m.audit_cross_files(root,bad,binding)
 def test_cross_requires_actual_objects(self):
  s=importlib.util.spec_from_file_location('fused_cross_test',ROOT/'scripts/hunet_fused/cross.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)
   for receipt in ({},{'exit':0,'arch':'AArch64','objects':['fake.o'],'scope':'FUSED_CROSS'}):
    with self.assertRaises(ValueError):m.audit_cross_files(root,receipt,bytes(512))
if __name__=='__main__':unittest.main()
