"""Software-line gates are distinct from hardware and board evidence."""
import copy,json,pathlib,tempfile,unittest
from unittest import mock
from scripts.hunet_opt import image

class ImageTests(unittest.TestCase):
 def test_image_software_gate_and_contents(self):
  m=dict(source_commit='a'*40,source_digest='b'*64,binding_sha256='c'*64,bit_sha256=image.BIT,xsa_sha256=image.XSA,candidate='COMBINED',profile=4,program_id=10)
  soft=dict(scope='UBUNTU_SOFTWARE_NOT_GPU_NOT_BOARD',natural_exit=0,source_digest=m['source_digest'])
  cross=dict(scope='OPT_KERNEL_AARCH64_BUILD',exit=0,source_commit=m['source_commit'],source_digest=m['source_digest'],audit=dict(integrity='PASS'))
  self.assertEqual(image.check_gates(m,soft,cross)['hardware'],'FROZEN_HW4_REUSED_NOT_RETESTED')
  for which,field,value in [('soft','scope','MOCK'),('soft','natural_exit',1),('soft','source_digest','0'*64),('cross','exit',1),('cross','source_commit','0'*40),('cross','source_digest','0'*64),('m','bit_sha256','0'*64),('m','xsa_sha256','0'*64),('m','profile',5),('m','program_id',9)]:
   rows=dict(m=copy.deepcopy(m),soft=copy.deepcopy(soft),cross=copy.deepcopy(cross));rows[which][field]=value
   with self.subTest(which=which,field=field),self.assertRaises(ValueError):image.check_gates(rows['m'],rows['soft'],rows['cross'])
  for s,k in [(None,cross),(soft,None)]:
   with self.assertRaises(ValueError):image.check_gates(m,s,k)
  expected=dict(source='a',crypto=True,builtin=True,dt=True,program='p',cpu='c',libm='m',binding='b',fit=True,full_source=True,library='l',rootfs='new')
  self.assertEqual(image.content_errors(expected,expected),[])
  for key in expected:
   bad=dict(expected);bad[key]=None
   self.assertEqual(image.content_errors(bad,expected),['IMAGE_CONTENT:'+key])
  # Absence of this run's RTL is intentional and never generates RTL PASS.
  result=image.check_gates(m,soft,cross)
  self.assertNotIn('rtl',result);self.assertEqual(result['board'],'NOT_RUN')
 def test_actual_recipe_and_configuration(self):
  recipe=image.recipes('a'*40,'b'*32,'COMBINED')
  self.assertIn('vxl_opt_release.h',recipe['kernel']);self.assertIn('COMBINED/catalog.h',recipe['kernel'])
  self.assertIn('vxl_hunet_opt',recipe['apps']);self.assertIn('libvortex-opt.so',recipe['apps'])
  self.assertIn('program10/kernel.elf',recipe['apps']);self.assertNotIn('program9/kernel.elf',recipe['apps'])
  self.assertIn('do_compile() {\n    :\n}',recipe['apps'])
  for name in image.CRYPTO_CONFIG:self.assertIn(name,image.builtin_config().splitlines())
  self.assertIn('CONFIG_DEBUG_FS=y',image.builtin_config())
  with self.assertRaises(ValueError):image.recipes('a'*40,'b'*32,'../bad')
 def test_bound_module_and_closed_policy(self):
  (image.ROOT/'build').mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(image.ROOT/'build')) as td:
   p=pathlib.Path(td);(p/'source/driver').mkdir(parents=True);(p/'source/driver/vxl_linux.ko').write_bytes(b'ELF'+b'x'*512)
   r=dict(module_sha256=image.sha(p/'source/driver/vxl_linux.ko'))
   self.assertEqual(image.check_module(p,r,b'x'*512),r['module_sha256'])
   with self.assertRaises(ValueError):image.check_module(p,r,b'y'*512)
   (p/'source/driver/vxl_linux.ko').write_bytes(b'altered')
   with self.assertRaises(ValueError):image.check_module(p,r,b'x'*512)
  policy=json.loads((image.ROOT/'delivery/templates/opt-image-policy.json').read_bytes())
  self.assertEqual(policy['policy'],image.POLICY);self.assertFalse(policy['board_authorized']);self.assertFalse(policy['sd_authorized'])

if __name__=='__main__':unittest.main()
