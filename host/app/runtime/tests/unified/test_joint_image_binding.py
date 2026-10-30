"""Joint boot metadata admission; no compiled image or device claims."""
import copy,hashlib,json,pathlib,unittest
from scripts.unified.cross_common import provider
from scripts.unified.bind_kernel import identity_dtsi,firmware_inputs
from scripts.unified.audit_kernel import audit_device_tree
from scripts.unified.audit_image import payload_errors
from tests.unified.test_kernel_dt import fixture,BACKEND,string
from tests.unified import test_image_payload as old_payload

ROOT=pathlib.Path(__file__).resolve().parents[2]

class JointImageBinding(unittest.TestCase):
 def test_joint_dt_keeps_s8_addresses_and_exact_versioned_identity(self):
  p=provider(ROOT,4);fw=firmware_inputs(ROOT,4);cat=hashlib.sha256(fw['catalog']).hexdigest();exe=hashlib.sha256(fw['execution']).hexdigest()
  def change(nodes):
   n=nodes[BACKEND];n['compatible']=string(p['compatible'])
   for key,value in [('hardware-contract',p['hardware_contract']),('bit',p['bit_sha256']),('xsa',p['xsa_sha256']),('catalog',cat),('execution',exe)]:n['vortex,'+key+'-sha256']=bytes.fromhex(value)
  row=audit_device_tree(fixture(change),p,cat,exe)
  self.assertEqual(row['provider_flags'],4);self.assertFalse(row['hardware_hash_measured']);self.assertIsNone(row['gpu_raw_cycles'])
  self.assertIn('vortex,hw4-s8-joint-v1',identity_dtsi(p,cat,exe))
  for key in ('compatible','bit_sha256','xsa_sha256','hardware_contract'):
   bad=copy.deepcopy(p);bad[key]='vortex,hw4-s8-demo-v1' if key=='compatible' else 'aa'*32
   with self.subTest(key=key),self.assertRaises(ValueError):identity_dtsi(bad,cat,exe)
 def test_joint_cross_target_is_explicit_and_legacy_targets_stay_distinct(self):
  from scripts.unified.cross_common import aarch64_target
  self.assertEqual([aarch64_target(f) for f in (1,2,4)],['aarch64-s8','aarch64-p8','aarch64-s8-joint'])
  for bad in (True,0,3,5):
   with self.assertRaises(ValueError):aarch64_target(bad)
 def joint_payload(self):
  entries,manifest=old_payload.InstalledPayload().fixture();manifest['provider_flags']=4
  p=provider(ROOT,4);fw=firmware_inputs(ROOT,4)
  hw=dict(hardware_contract=p['hardware_contract'],bit=p['bit_sha256'],xsa=p['xsa_sha256'],compatible=hashlib.sha256(p['compatible'].encode()).hexdigest(),provider_flags=4)
  install=dict(version=2,hardware=hw,nn=[dict(id='hunet'),dict(id='mosaic')],image=dict(session=42),p8=dict(session=77),mosaic_export=dict(build_id=manifest['build_id']),catalog=dict(path='/usr/share/vxgpu/catalog.bin',sha256=hashlib.sha256(fw['catalog']).hexdigest()))
  def put(name,raw,mode=0o444):
   entries[name]=dict(kind='file',data=raw,mode=mode,uid=0,gid=0);manifest['files'][name]=dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),mode=mode)
  raw=json.dumps(install).encode();put('etc/vxgpud/installation.json',raw,0o400);put('etc/vxgpud/installation.sha256',(hashlib.sha256(raw).hexdigest()+'\n').encode(),0o400)
  put('etc/vxgpud/config.json',json.dumps(dict(hw,backend_mode='compute-v2')).encode(),0o600)
  put('usr/share/vxgpu/catalog.bin',fw['catalog']);put('lib/firmware/vxl/catalog-v1.bin',fw['catalog']);put('lib/firmware/vxl/execution-v1.bin',fw['execution'])
  for entry in fw['entries']:put('lib/firmware/vxl/programs/'+entry['entry_id']+'.bin',entry['payload'])
  return entries,manifest,put
 def test_joint_payload_requires_all_apps_exact_hardware_and_16_original_programs(self):
  e,m,_=self.joint_payload();self.assertEqual(payload_errors(e,m),[])
  for key in ('p8','image','mosaic_export'):
   e,m,put=self.joint_payload();obj=json.loads(e['etc/vxgpud/installation.json']['data']);obj[key]=None;raw=json.dumps(obj).encode();put('etc/vxgpud/installation.json',raw,0o400);put('etc/vxgpud/installation.sha256',(hashlib.sha256(raw).hexdigest()+'\n').encode(),0o400)
   with self.subTest(key=key):self.assertTrue(payload_errors(e,m))
  e,m,put=self.joint_payload();put('lib/firmware/vxl/catalog-v1.bin',firmware_inputs(ROOT,1)['catalog']);self.assertTrue(payload_errors(e,m))
  e,m,put=self.joint_payload();obj=json.loads(e['etc/vxgpud/config.json']['data']);obj['compatible']='aa'*32;put('etc/vxgpud/config.json',json.dumps(obj).encode(),0o600);self.assertTrue(payload_errors(e,m))

if __name__=='__main__':unittest.main()
