import importlib,json,struct,unittest,hashlib,tempfile,csv,io
from scripts.s8.layout import initial_image
from tests.s8.whole_fixture import create,reseal,dump
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Audit(unittest.TestCase):
 def test_production_negative_assets_all_cases_and_slots(self):
  from scripts.s8.assets import payloads
  from scripts.s8.catalog import expected_cases
  from scripts.s8.oracle import matmul
  from scripts.s8.audit_run import audit_slot
  for case in [c for c in expected_cases() if c['id']>>8>=201]:
   original=(ROOT/('tests/s8/fixtures/negative/negative-%d.bin'%case['id'])).read_bytes()
   for slot in range(16):
    core,warp=divmod(slot,4);raw=bytearray(initial_image(5,slot))
    for off,n in ((64,384),(448,512),(960,256),(1216,512),(2304,384),(2688,512)):raw[off:off+n]=original[off:off+n]
    struct.pack_into('<3I',raw,0x3c0+28,core,warp,warp)
    a,b=payloads(case,slot);aa=[list(struct.unpack_from('<8h',a,t*16)) for t in range(2)];bb=[list(struct.unpack_from('<8h',b,t*16)) for t in range(2)]
    for k,r,d in ([(2,64,0x4c0)] if case['id']>>8 not in (201,208,209) else [])+[(1,0x900,0xa80)]:
     vals=matmul(aa[:k],bb[:k]);raw[r:r+384]=b''.join(v.to_bytes(6,'little',signed=True) for v in vals);struct.pack_into('<64q',raw,d,*vals)
    job=dict(case_id=case['id'],k=2,flags=0,core_mask=1<<core,owner_warp=warp,generation=2,sequence=1,run_id=bytes([1])+bytes(15),binding_tag=bytes([2])+bytes(15))
    self.assertEqual(audit_slot(job,bytes(raw),core,warp,inputs=(a,b))['recovery_exact'],64)
    raw[0x900]^=1
    with self.assertRaises(ValueError):audit_slot(job,bytes(raw),core,warp,inputs=(a,b))
 def test_actual_image_receipt_matches_immutable_package_without_manifest_edit(self):
  from scripts.s8.audit_run import validate_image_acceptance
  release=dict(scope='S8_RELEASE',source_sha256='a'*64,binding_sha256='b'*64,digests=dict(bit='c'*64,xsa='d'*64),hardware=dict(state='EXPORT_IDENTITY_VERIFIED_NOT_PHYSICAL_PASS'))
  receipt=dict(scope='ACTUAL_S8_IMAGE_CONTENT_NOT_BOARD',integrity='PASS',exit=0,source_sha256='a'*64,binding_sha256='b'*64,release_manifest_sha256='e'*64,bit_sha256='c'*64,xsa_sha256='d'*64,files={n:dict(sha256='f'*64,bytes=1) for n in ('BOOT.BIN','boot.scr','image.ub')})
  self.assertTrue(validate_image_acceptance(release,receipt,'e'*64))
  for key,val in [('binding_sha256','0'*64),('integrity','FAIL'),('release_manifest_sha256','0'*64),('scope','SW_AUDITOR_ONLY')]:
   with self.assertRaises(ValueError):validate_image_acceptance(release,dict(receipt,**{key:val}),'e'*64)
 def test_original_18_production_emitter_negative_records(self):
  api=importlib.import_module('scripts.s8.audit_run');self.assertTrue(hasattr(api,'audit_slot'),'independent slot audit missing')
  folder=ROOT/'tests/s8/fixtures/negative'
  receipt=json.loads((folder/'origin.json').read_text())
  self.assertEqual(receipt['scope'],'SOFTWARE_TEST_ONLY')
  for item in receipt['files']:
   original=(folder/item['path']).read_bytes();self.assertEqual(hashlib.sha256(original).hexdigest(),item['sha256']);raw=bytearray(initial_image(5,0))
   # Producer fixture used cc outside result regions. Wrap its unchanged raw
   # payloads in the canonical guarded object; original files stay immutable.
   for offset,size in ((64,384),(448,512),(960,256),(1216,512),(2304,384),(2688,512)):raw[offset:offset+size]=original[offset:offset+size]
   raw=bytes(raw);case=item['case_id'];job=dict(case_id=case,k=2,flags=0,core_mask=1,owner_warp=0,generation=2,sequence=1,run_id=bytes([1])+bytes(15),binding_tag=bytes([2])+bytes(15))
   historic=(struct.pack('<16h',*(i+1 for t in range(2) for i in range(8))),struct.pack('<16h',*((t+1)*(i+1) for t in range(2) for i in range(8))))
   out=api.audit_slot(job,raw,0,0,inputs=historic);self.assertEqual(out['classification'],'EXPECTED_PROTOCOL_ERROR');self.assertEqual(out['recovery_exact'],64)
   for offset in (0x900,0xa80,0x1c0+8,0x1c0+24,0x3c0+20,0x3c0+120,4032):
    bad=bytearray(raw);bad[offset]^=1
    with self.assertRaises(ValueError,msg=(case,offset)):api.audit_slot(job,bytes(bad),0,0,inputs=historic)
 def test_raw48_extreme_and_slot_identity_no_historical_exemption(self):
  api=importlib.import_module('scripts.s8.audit_run');self.assertTrue(hasattr(api,'audit'),'complete run audit missing')
  with self.assertRaises(ValueError):api.audit(Path('missing-s8-evidence'),Path('missing-s8-manifest'))
  with tempfile.TemporaryDirectory() as d:
   root,manifest=create(Path(d),113);job=api.parse_job((root/'job.bin').read_bytes());job['historical_equal']=True;data=(root/'capture-000-81400000.bin').read_bytes();self.assertEqual(api.audit_slot(job,data,0,0)['exact'],64)
   for value in (-(1<<47),(1<<47)-1):
    bad=bytearray(data);bad[64:70]=value.to_bytes(6,'little',signed=True)
    with self.assertRaises(ValueError):api.audit_slot(job,bytes(bad),0,0)
 def test_complete_synthetic_package_and_semantic_tampering(self):
  api=importlib.import_module('scripts.s8.audit_run')
  with tempfile.TemporaryDirectory() as d:
   root,manifest=create(Path(d),115);result=api.audit(root,manifest);self.assertEqual(result['scope'],'SW_AUDITOR_ONLY');self.assertEqual(result['board_state'],'NOT_RUN');self.assertIsNone(result['gpu_raw_cycles']);self.assertEqual(result['slots'][0]['exact'],64)
   for name,offset in [('capture-000-81400000.bin',64),('capture-000-81400000.bin',0x1c0+8),('capture-000-81400000.bin',0x3c0+20),('capture-001-81401000.bin',100),('capture-016-81410000.bin',64+20),('capture-018-800f0000.bin',0)]:
    p=root/name;original=p.read_bytes();bad=bytearray(original);bad[offset]^=1;p.write_bytes(bad);reseal(root)
    with self.assertRaises(ValueError,msg=(name,offset)):api.audit(root,manifest)
    p.write_bytes(original);reseal(root)
   for field,value in [('pending_w','1'),('jobs','9'),('cdma_status','16'),('crt_guards','00'*192),('elapsed_raw','0'),('event3_first_ns','2000')]:
    p=root/'jobs.csv';original=p.read_bytes();lines=original.decode().splitlines();rows=list(csv.DictReader(io.StringIO('\n'.join(lines[1:]))));rows[0][field]=value;s=io.StringIO();w=csv.DictWriter(s,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows);p.write_text(lines[0]+'\n'+s.getvalue());reseal(root)
    with self.assertRaises(ValueError,msg=field):api.audit(root,manifest)
    p.write_bytes(original);reseal(root)
   command=json.loads((root/'command.json').read_text());command['exit']=1;dump(root/'command.json',command)
   with self.assertRaises(ValueError):api.audit(root,manifest)
   forensic=api.audit_failure(root,manifest);self.assertEqual(forensic['state'],'FAIL_NOT_ACCEPTED');self.assertIsNone(forensic['gpu_raw_cycles'])
   command['exit']=0;dump(root/'command.json',command);seal=json.loads((root/'case-manifest.json').read_text());seal['scope']='REAL_DRIVER_CAPTURE_PENDING_INDEPENDENT_AUDIT';dump(root/'case-manifest.json',seal);reseal(root)
   with self.assertRaises(ValueError):api.audit(root,manifest)
if __name__=='__main__':unittest.main()
