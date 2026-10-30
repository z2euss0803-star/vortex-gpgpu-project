import copy, importlib.util, json, sys, unittest, tempfile, shutil, os, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
from audit_run import audit_hybrid, formal_gate
import audit_run as aud
from unittest.mock import patch
from audit_fixture import fixture, sha, js

class AuditNegativeTests(unittest.TestCase):
 def test_missing_raw_never_passes(self):
  a=audit_hybrid(ROOT/'build/no-such-run',ROOT/'build/no-such-release',{})
  self.assertEqual(a['integrity'],'FAIL');self.assertFalse(a['publish_performance'])
 def test_formal_gate_refuses_old_failure_or_other_epoch(self):
  a=dict(integrity='PASS',scope='HW4_HYBRID_READONLY_PREFLIGHT',mode='diagnostic',R2={'status':'PASS'},ORT={'status':'FAIL','mismatch_count':8081},run_id='1'*32,boot_id='boot',binding_sha256='2'*64)
  self.assertFalse(formal_gate(a,dict(boot_id='boot',binding_sha256='2'*64),True))
  a['ORT']['status']='PASS'
  self.assertTrue(formal_gate(a,dict(boot_id='boot',binding_sha256='2'*64),True))
  self.assertFalse(formal_gate(a,dict(boot_id='other',binding_sha256='2'*64),True))
  self.assertFalse(formal_gate(a,dict(boot_id='boot',binding_sha256='2'*64),False))

class CompleteProtocolTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name);cls.fixture_run,cls.rel,cls.refs=fixture(cls.root)
 def setUp(self):
  self.new=self.root/('clone-'+str(__import__('time').monotonic_ns()));shutil.copytree(self.fixture_run,self.new,copy_function=os.link);self.refs=copy.deepcopy(type(self).refs)
 def tearDown(self):shutil.rmtree(self.new)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def replace(self,name,data):
  p=self.new/name;p.unlink();p.write_bytes(data);m=json.loads((self.new/'run.json').read_bytes());m['files'][name]=dict(bytes=len(data),sha256=sha(data));self.meta(m)
 def meta(self,m):
  p=self.new/'run.json';p.unlink();js(p,m)
 def audit(self):
  with patch.object(aud.old,'numeric',return_value={'R2_numeric':{'status':'PASS'},'ORT_application_numeric':{'status':'FAIL','mismatch_count':8081},'Linux-v5_same_math':{'status':'FAIL'}}):return audit_hybrid(self.new,self.rel,self.refs)
 def test_full_protocol_is_not_numeric_or_board_pass(self):
  a=self.audit();self.assertEqual(a['integrity'],'PASS',a);self.assertEqual(len(a['gpu']),1010);self.assertEqual(len(a['per_layer_cpu']),13);self.assertEqual(a['ORT']['mismatch_count'],8081);self.assertEqual(a['v5_bitwise_relation'],'DIFFERENT');self.assertFalse(a['publish_performance']);self.assertFalse(a['formal_allowed']);self.assertIsNone(a['gpu_seconds']);self.assertEqual(len(a['topk']['candidate']),5);self.assertIsNone(a['gpu_seconds_conditional'])
 def test_invalid_clock_metadata_cannot_leave_pass(self):
  for hz in [0,-1,'96969696',1.2]:
   self.refs['gate']['framework_hz']=hz;a=self.audit()
   self.assertEqual(a['stop_reason'],'CONDITIONAL_FRAMEWORK_HZ')
   self.assertEqual(a['integrity'],'FAIL');self.assertFalse(a['formal_allowed']);self.assertFalse(a['publish_performance'])
 def test_role_count_and_epoch(self):
  m=json.loads((self.new/'run.json').read_bytes());m['gpu_records']=1023;self.meta(m);self.assertIn('ROLE_COUNTS',self.audit()['stop_reason'])
 def test_old_capture_run_injection(self):
  b=bytearray((self.new/'cpu/008-begin.bin').read_bytes());b[16]=0x42;self.replace('cpu/008-begin.bin',b);self.assertIn('CPU_RUN',self.audit()['stop_reason'])
 def test_stale_boot(self):
  self.refs['gate']['boot_id']='OTHER';self.assertIn('STALE_EPOCH',self.audit()['stop_reason'])
 def test_cpu_delta_and_writeback(self):
  b=bytearray((self.new/'cpu/008-evidence.bin').read_bytes());struct.pack_into('<I',b,220,296);self.replace('cpu/008-evidence.bin',b);self.assertIn('CPU_ZERO_GPU_DELTA',self.audit()['stop_reason'])
 def test_cpu_writeback_and_reference(self):
  b=bytearray((self.new/'cpu/008-writeback.f32').read_bytes());b[0]=1;self.replace('cpu/008-writeback.f32',b);self.assertIn('CPU_WRITEBACK',self.audit()['stop_reason'])
 def test_reference_is_independent_required_artifact(self):
  b=bytearray((self.new/'cpu/008-reference.f32').read_bytes());b[0]=1;self.replace('cpu/008-reference.f32',b);self.assertIn('CPU_INDEPENDENT_REFERENCE_BITWISE',self.audit()['stop_reason'])
 def test_fake_cpu_cycles(self):
  t=json.loads((self.new/'timing.json').read_bytes());t['cpu'][0]['elapsed_cycles']=100;self.replace('timing.json',json.dumps(t).encode());self.assertEqual(self.audit()['integrity'],'FAIL')
 def test_missing_gpu_cycles_validity(self):
  b=bytearray((self.new/'gpu/001-evidence.bin').read_bytes());struct.pack_into('<I',b,124,25);self.replace('gpu/001-evidence.bin',b);self.assertIn('GPU_IDLE_VALID_CYCLES',self.audit()['stop_reason'])
 def test_missing_or_duplicate_gpu_timing(self):
  t=json.loads((self.new/'timing.json').read_bytes());t['gpu'][1]=t['gpu'][0];self.replace('timing.json',json.dumps(t).encode());self.assertIn('GPU_TIMING_MISSING_DUPLICATE',self.audit()['stop_reason'])
 def test_libm_or_compiler_drift(self):
  self.refs['gate']['libm_sha256']='3'*64;self.assertIn('CPU_RUNTIME_IDENTITY',self.audit()['stop_reason'])
 def test_end_binding_or_counter_drift(self):
  b=bytearray((self.new/'end.bin').read_bytes());b[144]^=1;self.replace('end.bin',b);self.assertIn('END_FULL_HASH',self.audit()['stop_reason'])
 def test_fail_stop_does_not_publish(self):
  m=json.loads((self.new/'run.json').read_bytes());m['state']='FAIL_STOP';m['error']=-5;self.meta(m);self.assertIn('FAIL_STOP',self.audit()['stop_reason'])
 def test_prefix_is_never_full_end_or_performance(self):
  m=json.loads((self.new/'run.json').read_bytes());keep=set(m['files'])
  for n in list(keep):
   reject=(n.startswith(('gpu/','logical/','metrics/')) and int(n.split('/')[1].split('-')[0])>392) or (n.startswith('cpu/') and not n.startswith('cpu/008-')) or (n.startswith('stages/') and int(n.split('/')[1].split('.')[0])>9) or (n.startswith('traces/') and int(n.split('/')[1].split('.')[0])>392) or n in ['end.bin','guards.bin','last_trace.u32','logits.f32']
   if reject:(self.new/n).unlink();keep.remove(n)
  m['files']={n:v for n,v in m['files'].items() if n in keep};m.update(mode='first-gelu',state='PREFIX_STOP',logical_completed=392,gpu_records=391,cpu_records=1,T_diagnostic_capture_ns=39100);self.meta(m)
  self.replace('logical_args.bin',(self.rel/'base/jobs.bin').read_bytes()[:392*128]);t=json.loads((self.new/'timing.json').read_bytes());t['gpu']=[r for r in t['gpu'] if r['sequence']<=392];t['cpu']=t['cpu'][:1];self.replace('timing.json',json.dumps(t).encode())
  js(self.new/'prefix-stop.json',dict(state='PREFIX_STOP',logical_jobs=392,gpu_jobs=391,cpu_jobs=1));m=json.loads((self.new/'run.json').read_bytes());b=(self.new/'prefix-stop.json').read_bytes();m['files']['prefix-stop.json']=dict(bytes=len(b),sha256=sha(b));self.meta(m)
  for j in struct.iter_unpack('<32I',(self.rel/'base/jobs.bin').read_bytes()):
   if j[30]==9 and j[3]==1:
    name='packs/%04u.f16'%j[4];p=self.new/name;p.parent.mkdir(exist_ok=True);p.write_bytes(bytes(j[7]*2));m=json.loads((self.new/'run.json').read_bytes());m['files'][name]=dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes()));self.meta(m)
  a=self.audit();self.assertEqual(a['integrity'],'PASS',a['stop_reason']);self.assertEqual(a['ORT']['status'],'NOT_EVALUATED');self.assertFalse(a['publish_performance']);self.assertFalse(a['formal_allowed'])

if __name__=='__main__':unittest.main()
