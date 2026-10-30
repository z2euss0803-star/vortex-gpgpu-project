import json,struct,sys,tempfile,unittest
from pathlib import Path
from evidence_fixture import ROOT,fixture,rehash,references
sys.path.insert(0,str(ROOT/'scripts/hunet'))
from audit_run import audit_run,numeric,read_npy_row
class Evidence(unittest.TestCase):
 def setUp(self):
  tmp=ROOT/'build/test-tmp';tmp.mkdir(parents=True,exist_ok=True);self.t=tempfile.TemporaryDirectory(dir=tmp);self.run,self.release,self.refs=fixture(Path(self.t.name))
 def tearDown(self):self.t.cleanup()
 def test_changed_acceptance_and_same_version_reference_rejected(self):
  import copy,hashlib
  for role in ['original_acceptance','v5']:
   refs=copy.deepcopy(self.refs);record=refs['v5']['0'] if role=='v5' else refs[role]
   raw=Path(record['path']).read_bytes()
   if role=='original_acceptance':
    value=json.loads(raw);value['device_logits_R2']['atol']=1000;raw=json.dumps(value).encode()
   else:
    b=bytearray(raw);b[0]^=1;raw=bytes(b)
   p=Path(self.t.name)/(role+'.replacement');p.write_bytes(raw)
   record.update(path=str(p),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
   self.assertEqual(audit_run(self.run,self.release,refs)['integrity'],'FAIL',role)
   self.assertRaises(ValueError,numeric,(self.run/'logits.f32').read_bytes(),0,refs)

 def test_identical_reference_relocation_is_allowed(self):
  import copy
  refs=copy.deepcopy(self.refs)
  for name,record in [('acceptance',refs['original_acceptance']),('R2',refs['original_R2']),('ORT',refs['original_ORT_FP32']),('bare0',refs['v5']['0']),('bare26',refs['v5']['26'])]:
   p=Path(self.t.name)/name;p.write_bytes(Path(record['path']).read_bytes());record['path']=str(p)
  self.assertEqual(audit_run(self.run,self.release,refs)['integrity'],'PASS')
 def test_whole_graph_monitor_limit_is_audit_bound(self):
  p=self.run/'run.json';m=json.loads(p.read_text());m['T_resident_ns']=180000000001;p.write_text(json.dumps(m))
  self.assertEqual(audit_run(self.run,self.release,self.refs)['integrity'],'FAIL')

 def test_full_protocol_fixture(self):
  a=audit_run(self.run,self.release,self.refs);self.assertEqual(a['integrity'],'PASS',a);self.assertEqual(a['scope'],'TEST_PROTOCOL_NOT_BOARD');self.assertEqual(a['Linux-v5_same_math']['status'],'PASS');self.assertEqual(a['R2_numeric']['mismatch_count'],0);self.assertEqual(a['ORT_application_numeric']['mismatch_count'],8081)
 def test_stale_epoch_rejected(self):
  b=bytearray((self.run/'job_evidence.bin').read_bytes());struct.pack_into('<I',b,152,4);(self.run/'job_evidence.bin').write_bytes(b);rehash(self.run,'job_evidence.bin');self.assertEqual(audit_run(self.run,self.release,self.refs)['integrity'],'FAIL')
 def test_args_receipt_tamper_rejected(self):
  b=bytearray((self.run/'job_evidence.bin').read_bytes());b[160]^=1;(self.run/'job_evidence.bin').write_bytes(b);rehash(self.run,'job_evidence.bin');self.assertEqual(audit_run(self.run,self.release,self.refs)['integrity'],'FAIL')
 def test_full_logits_required(self):
  p=self.run/'logits.f32';p.write_bytes(p.read_bytes()[:-4]);rehash(self.run,'logits.f32');self.assertEqual(audit_run(self.run,self.release,self.refs)['integrity'],'FAIL')
 def test_bad_early_job_not_hidden(self):
  b=bytearray((self.run/'job_evidence.bin').read_bytes());struct.pack_into('<I',b,84,1);(self.run/'job_evidence.bin').write_bytes(b);rehash(self.run,'job_evidence.bin');self.assertEqual(audit_run(self.run,self.release,self.refs)['integrity'],'FAIL')
 def test_frozen_baremetal_numeric_failures(self):
  refs=references()
  for roi,expected in [(0,(0,8081)),(26,(2,7767))]:
   a=numeric(Path(refs['v5'][str(roi)]['path']).read_bytes(),roi,refs);self.assertEqual((a['R2_numeric']['mismatch_count'],a['ORT_application_numeric']['mismatch_count']),expected)
  self.assertEqual(numeric(Path(refs['v5']['0']['path']).read_bytes(),39,refs)['Linux-v5_same_math']['status'],'NO_FROZEN_REFERENCE')
 def test_npy_rejects_pickle_unknown_and_truncation(self):
  p=Path(self.t.name)/'bad.npy';p.write_bytes(b'\x93NUMPY\x01\x00\x10\x00'+b"{'descr':'|O'}  "+b'bad');self.assertRaises(ValueError,read_npy_row,p,0)
if __name__=='__main__':unittest.main()
