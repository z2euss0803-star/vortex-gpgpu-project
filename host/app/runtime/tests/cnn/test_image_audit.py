import importlib.util,sys,unittest,json,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/cnn'))
class ImageAudit(unittest.TestCase):
 def module(self):
  path=ROOT/'scripts/cnn/image_audit.py';self.assertTrue(path.exists(),'Matching image auditor missing')
  s=importlib.util.spec_from_file_location('image_audit',path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
 def test_identity(self):
  m=self.module();commit='a'*40;build='H0-LINUX-CNN-I32-V1-20261011T000000Z';good=m.identity(commit,build)
  self.assertEqual(m.identity_errors(good,commit,build),[])
  for key,value in [('scope','CPU_MOCK'),('source_commit','b'*40),('build_id','H0-LINUX-BACKEND-FIX1-old'),('args_bytes',128),('programs',good['programs'][:-1]),('bit_sha256','0'*64),('profile','hw4')]:
   bad=dict(good);bad[key]=value;self.assertTrue(m.identity_errors(bad,commit,build))
  bad=json.loads(json.dumps(good));bad['programs'][0]['sha256']='0'*64;self.assertTrue(m.identity_errors(bad,commit,build))
 def test_kernel_and_elf_rejections(self):
  m=self.module()
  self.assertTrue(m.kernel_errors('CONFIG_VXL_LINUX=m',''))
  self.assertTrue(m.kernel_errors('CONFIG_VXL_LINUX=y',' vxl_init\n'))
  self.assertEqual(m.kernel_errors('CONFIG_VXL_LINUX=y\nCONFIG_ZONE_DMA32=y\n',' vxl_init\n vxl_engine_poll\n vxl_cnn_args_check\n'),[])
  for machine,interpreter in [('X86-64','/lib64/ld-linux-x86-64.so.2'),('AArch64','/lib/ld-linux.so.3'),('ARM','/lib/ld-linux-aarch64.so.1')]:
   self.assertTrue(m.elf_errors('Machine: '+machine,'[Requesting program interpreter: '+interpreter+']',True))
  self.assertEqual(m.elf_errors('Machine: AArch64','[Requesting program interpreter: /lib/ld-linux-aarch64.so.1]',True),[])
 def test_missing_rootfs_payload_and_old_kernel_source(self):
  m=self.module();required=set(m.REQUIRED_ROOTFS)
  self.assertEqual(m.rootfs_missing(required),[])
  for name in ('usr/bin/vxl_cnn','usr/share/codex-cnn/id3.bin','etc/codex-build-id'):
   self.assertIn(name,m.rootfs_missing(required-{name}))
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);snapshot=p/'snapshot';actual=p/'actual'
   for root in (snapshot,actual):(root/'core').mkdir(parents=True)
   (snapshot/'core/cnn.c').write_bytes(b'current controlled CNN preflight')
   inventory={'core/cnn.c':m.sha(snapshot/'core/cnn.c')}
   (actual/'core/cnn.c').write_bytes(b'previous FIX1 preflight')
   self.assertTrue(m.source_errors(snapshot,actual,inventory)[0])
   (actual/'core/cnn.c').write_bytes((snapshot/'core/cnn.c').read_bytes())
   self.assertEqual(m.source_errors(snapshot,actual,inventory)[0],[])
if __name__=='__main__':unittest.main()
