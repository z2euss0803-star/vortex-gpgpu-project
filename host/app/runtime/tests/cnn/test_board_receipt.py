"""Offline rejection tests; synthetic inputs never constitute board evidence."""
import copy,hashlib,json,os,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/cnn'))
from image_audit import identity

class BoardReceipt(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.folder=Path(self.temp.name)/'run';self.folder.mkdir()
  self.expected=identity('e5f55b2baa6ff04763658862c9563cfa2e34aa2f','H0-LINUX-CNN-I32-V1-20261011T070112Z')
  self.boot='12345678-1234-1234-1234-123456789abc'
  # Reuse actual protocol-test output only as an offline synthetic fixture.
  parent=ROOT/os.environ.get('CNN_OUT','build/pre-rtl-20261011')/'session-evidence'
  path=max(parent.iterdir(),key=lambda p:int(p.name))
  import shutil
  shutil.copytree(path,self.folder,dirs_exist_ok=True)
  self.run=json.loads((self.folder/'run.json').read_text())
  self.run.update(scope='REAL_DRIVER_CNN_I32_V1',source_commit=self.expected['source_commit'],build_id=self.expected['build_id'],boot_id=self.boot,kernel='5.4.0-xilinx-v2020.1 #1 SMP aarch64')
  self.records=[json.loads(l) for l in (self.folder/'timing.jsonl').read_text().splitlines()]
  for r in self.records:r['scope']='REAL_DRIVER_CNN_I32_V1'
  self.save()
 def tearDown(self):self.temp.cleanup()
 def save(self):
  (self.folder/'run.json').write_text(json.dumps(self.run))
  (self.folder/'release-manifest.json').write_text(json.dumps(self.expected))
  (self.folder/'timing.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in self.records))
 def check(self):
  self.assertTrue((ROOT/'scripts/cnn/board_receipt.py').is_file(),'Independent board receipt auditor missing')
  from board_receipt import verify
  return verify(self.folder,self.expected,self.boot)
 def test_complete_synthetic_structure(self):self.assertEqual(self.check()['errors'],[])
 def test_identity_and_cleanup_rejected(self):
  original=copy.deepcopy(self.run)
  changes={'scope':'TEST_PROTOCOL_NOT_GPU','build_id':'OLD','boot_id':'different','kernel':'x86_64','source_commit':'0'*40,'close_result':-4,'pinned_retained':True,'clock_resolution_ns':0,'final_info_valid':False}
  for field,value in changes.items():
   self.run=copy.deepcopy(original);self.run[field]=value;self.save()
   self.assertTrue(self.check()['errors'],field)
 def test_each_early_or_late_failure_rejected(self):
  original=copy.deepcopy(self.records)
  for index in (0,1,3,4,23):
   for change in ('scope','guard','state','jobs','pending','hash','raw_valid','clock','metric','copy','layer_bytes','phase','fence'):
    self.records=copy.deepcopy(original);r=self.records[index];l=r['layers'][0]
    if change=='scope':r['scope']='TEST_PROTOCOL_NOT_GPU'
    elif change=='guard':l['guard_pass']=False
    elif change=='state':l['snapshot']['state']=1
    elif change=='jobs':l['snapshot']['jobs']+=1
    elif change=='pending':l['snapshot']['pending_r']=1
    elif change=='hash':l['program_sha256']='0'*64
    elif change=='raw_valid':l['raw_valid'][8]=False
    elif change=='clock':l['start_ns']=r['end_ns']+1
    elif change=='metric':r['cpu_reference_ns']=None
    elif change=='copy':r['transfers'][0]['confirmed_bytes']-=1
    elif change=='layer_bytes':l['bytes']['d2h']+=1
    elif change=='phase':r['phase']='WARMUP' if r['phase']=='MEASURED' else 'MEASURED'
    elif change=='fence':l['snapshot']['hwstatus']=0
    self.save();self.assertTrue(self.check()['errors'],(index,change))
 def test_raw_missing_mutated_and_extra_rejected(self):
  path=self.folder/'raw/iteration-0-layer-0-object-0.bin';original=path.read_bytes()
  path.unlink();self.assertTrue(self.check()['errors'])
  path.write_bytes(bytes([original[0]^1])+original[1:]);self.assertTrue(self.check()['errors'])
  path.write_bytes(original);(self.folder/'raw/extra.bin').write_bytes(b'fake');self.assertTrue(self.check()['errors'])
 def test_insufficient_samples_rejected(self):
  self.records=self.records[:-1];self.save();self.assertTrue(self.check()['errors'])
 def test_wrong_manifest_rejected(self):
  bad=copy.deepcopy(self.expected);bad['profile']='hunet-hw4'
  (self.folder/'release-manifest.json').write_text(json.dumps(bad));self.assertTrue(self.check()['errors'])
 def test_duplicate_json_rejected(self):
  p=self.folder/'run.json';p.write_text('{"scope":"TEST_PROTOCOL_NOT_GPU",'+p.read_text()[1:]);self.assertTrue(self.check()['errors'])
 def test_done_plus_error_rejected(self):
  self.records[0]['layers'][0]['snapshot']['hwstatus']=6;self.save();self.assertTrue(self.check()['errors'])
if __name__=='__main__':unittest.main()
