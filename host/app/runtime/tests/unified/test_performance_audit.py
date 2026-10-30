import copy,json,os,pathlib,subprocess,sys,tempfile,unittest
from scripts.unified.audit_performance import audit,compare_candidates

class PerformanceAudit(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  name=os.environ.get('U19_EVENT_FIXTURE');assert name,'actual C++ serializer fixture required'
  cls.path=pathlib.Path(name);cls.row=json.loads(cls.path.read_text())
 def test_roundtrip(self):
  report=audit([self.row]);self.assertIn('groups',report,'U19_AUDIT_ASSERT bound-event groups required');self.assertIsNone(report['gpu_raw_cycles']);self.assertEqual(report['groups'][0]['wall_ns'],1000);self.assertEqual(report['groups'][0]['source_scope'],'HOST_PROTOCOL_TEST')
 def test_strict_missing_extra_and_scalar_fields(self):
  for name in self.row:
   bad=copy.deepcopy(self.row);del bad[name]
   with self.subTest(missing=name),self.assertRaises(ValueError):audit([bad])
  for name,value in [('extra',1),('event_id',0),('bytes',1.5),('bytes',1<<64),('cycles_valid',1),('parent_event',0),('end_ns',-1),('clock_domain','GPU_COUNTER'),('scope','ASSUMED_PASS'),('hardware_sha','0'*64),('namespace_id',2),('hardware_identity_source','PHYSICALLY_MEASURED')]:
   bad=copy.deepcopy(self.row);bad[name]=value
   with self.subTest(field=name,value=value),self.assertRaises(ValueError):audit([bad])
 def test_nested_and_clock_groups(self):
  root=copy.deepcopy(self.row);load=copy.deepcopy(root);load.update(event_id=2,parent_event=1,start_ns=100,end_ns=300,phase='weight_load');guard=copy.deepcopy(root);guard.update(event_id=3,parent_event=2,start_ns=200,end_ns=250,phase='guard');cpu=copy.deepcopy(root);cpu.update(event_id=4,parent_event=1,start_ns=400,end_ns=700,phase='cpu_gelu')
  g=audit([root,load,guard,cpu])['groups'][0];self.assertEqual((g['wall_ns'],g['exclusive_work_ns']),(1000,1000));self.assertEqual(g['phases']['weight_load']['exclusive_ns'],150);self.assertEqual(g['phases']['end_to_end']['exclusive_ns'],500)
  other=copy.deepcopy(root);other.update(host='B',clock_domain='B_MONOTONIC');self.assertEqual(len(audit([root,other])['groups']),2)
  other['parent_event']=1
  with self.assertRaises(ValueError):audit([root,other])
  with self.assertRaises(ValueError):audit([root,root])
  root['parent_event']=1
  with self.assertRaises(ValueError):audit([root])
 def test_raw32_zero_job_and_incomplete_total(self):
  first=copy.deepcopy(self.row);first.update(scope='REAL_BOARD',clock_domain='LINUX_MONOTONIC',counter_kind='GPU_JOB',cycles_valid=True,gpu_raw_cycles=0x80000005)
  second=copy.deepcopy(first);second.update(event_id=2,job_id='job1',start_ns=1001,end_ns=1002)
  report=audit([first,second]);self.assertIsNone(report['gpu_raw_cycles']);self.assertEqual(report['groups'][0]['declared_raw_counter_sum'],4294967306)
  for change in [dict(gpu_raw_cycles=1<<32),dict(gpu_raw_cycles=0),dict(scope='OFFLINE'),dict(clock_domain='HOST_MONOTONIC')]:
   bad=copy.deepcopy(first);bad.update(change)
   with self.subTest(change=change),self.assertRaises(ValueError):audit([bad])
  second['job_id']=first['job_id']
  with self.assertRaises(ValueError):audit([first,second])
  first.update(counter_kind='ZERO_JOB_CACHE',gpu_raw_cycles=0);self.assertEqual(audit([first])['groups'][0]['declared_raw_counter_sum'],0)
  first.update(counter_kind='GPU_JOB',gpu_raw_cycles=None,cycles_valid=False,status=-5,execution_error=dict(code=-5,category='ORIGINAL_GPU',detail='incomplete raw counter'))
  self.assertIsNone(audit([first])['groups'][0]['declared_raw_counter_sum']);self.assertEqual(audit([first])['groups'][0]['errors'][0]['execution_error'],first['execution_error'])
 def test_parallel_work_and_configured_frequency_are_separate(self):
  root=copy.deepcopy(self.row);root['end_ns']=100;children=[]
  for i in (2,3):
   child=copy.deepcopy(root);child.update(event_id=i,parent_event=1,phase='cpu_gelu');children.append(child)
  g=audit([root]+children)['groups'][0];self.assertEqual((g['wall_ns'],g['exclusive_work_ns']),(100,200));self.assertTrue(g['work_can_overlap'])
  e=copy.deepcopy(self.row);e.update(scope='REAL_BOARD',clock_domain='LINUX_MONOTONIC',counter_kind='GPU_JOB',cycles_valid=True,gpu_raw_cycles=970)
  g=audit([e],(97000000,1))['groups'][0];self.assertEqual(g['configured_frequency_estimate']['ns_floor'],10000);self.assertFalse(g['configured_frequency_estimate']['physical_measurement'])
 def test_original_error_and_candidate_identity(self):
  e=copy.deepcopy(self.row);e.update(status=-14,execution_error=dict(code=-14,category='RETURN_COPY',detail='original copy fault'));self.assertEqual(audit([e])['groups'][0]['errors'][0]['execution_error'],e['execution_error'])
  e['execution_error']['code']=14
  with self.assertRaises(ValueError):audit([e])
  binding={k:self.row[k] for k in ('hardware_sha','bit_sha','xsa_sha','compatible_sha','program_code_sha','program_schema_sha','model_sha','plan_sha','input_sha')};binding['numeric_reference_sha']='f'*64
  policy=dict(reuse_constants=False,batch_submit=False,weighted_dispatch=False,eliminate_redundant_layout=False);base=dict(binding=binding,policy=policy,accepted_receipt_sha='e'*64,copy_calls=3,copied_output_sha='a'*64);candidate=copy.deepcopy(base);candidate['policy']['eliminate_redundant_layout']=True;candidate['copy_calls']=2
  result=compare_candidates(base,candidate);self.assertEqual(result['copy_call_reduction'],1);self.assertIsNone(result['gpu_raw_cycles']);self.assertFalse(result['gpu_speedup_claim'])
  candidate['binding']['input_sha']='b'*64
  with self.assertRaises(ValueError):compare_candidates(base,candidate)
 def test_configured_estimate_overflow(self):
  e=copy.deepcopy(self.row);e.update(scope='REAL_BOARD',clock_domain='LINUX_MONOTONIC',counter_kind='GPU_JOB',cycles_valid=True,gpu_raw_cycles=970)
  with self.assertRaises(ValueError,msg='U19_AUDIT_ASSERT configured estimate overflow must be refused'):audit([e],(1,(1<<64)-1))
 def test_cli_creates_new_report_without_overwrite(self):
  with tempfile.TemporaryDirectory() as directory:
   output=pathlib.Path(directory)/'report.json';cmd=[sys.executable,'-m','scripts.unified.audit_performance',str(self.path),'--output',str(output)]
   p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE);self.assertEqual(p.returncode,0,p.stderr.decode());self.assertIsNone(json.loads(output.read_text())['gpu_raw_cycles']);p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE);self.assertNotEqual(p.returncode,0)
if __name__=='__main__':unittest.main(verbosity=2)
