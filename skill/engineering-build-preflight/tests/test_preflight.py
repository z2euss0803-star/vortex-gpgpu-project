import importlib.util,pathlib,unittest
P=pathlib.Path
S=importlib.util.spec_from_file_location('preflight',P(__file__).parents[1]/'scripts/check_preflight.py');M=importlib.util.module_from_spec(S);S.loader.exec_module(M)

def sample():
 return dict(stage='incremental-software',host='host-a',account='builder',physical_gib=2.5,commit_gib=3.5,heavy_jobs=0,tools_known=True,fresh=True,sample_after_environment_start=True,policy={'physical_gib':4,'commit_gib':8,'required_checks':[]},checks={},exception={'authorized':True,'stage':'incremental-software','host':'host-a','physical_gib':2,'commit_gib':3,'authorization_source':'current-task-confirmation'})
class PreflightTests(unittest.TestCase):
 def test_scoped_exception_keeps_original_refusal(self):
  r=M.evaluate(sample());self.assertEqual(r['decision'],'ALLOW_EXCEPTION');self.assertEqual(r['original_decision'],'REFUSED')
 def test_exception_does_not_extend_to_synthesis(self):
  d=sample();d['stage']='synthesis';self.assertEqual(M.evaluate(d)['decision'],'BLOCKED')
 def test_cold_or_stale_samples_unknown(self):
  for flag in ['fresh','sample_after_environment_start']:
   d=sample();d[flag]=False;self.assertEqual(M.evaluate(d)['decision'],'UNKNOWN')
 def test_unknown_query_not_idle(self):
  d=sample();d['heavy_jobs']=None;self.assertEqual(M.evaluate(d)['decision'],'UNKNOWN')
 def test_other_heavy_job_blocks(self):
  d=sample();d['heavy_jobs']=1;self.assertEqual(M.evaluate(d)['decision'],'BLOCKED')
 def test_required_simulation_cannot_be_waived_by_memory(self):
  d=sample();d['policy']['required_checks']=['functional_simulation'];d['checks']={'functional_simulation':'WAIVED_NOT_RUN'};self.assertEqual(M.evaluate(d)['decision'],'BLOCKED')
 def test_no_fixed_minimum(self):
  d=sample();d['physical_gib']=1;d['commit_gib']=1;d['policy']={'physical_gib':0.5,'commit_gib':0.5,'required_checks':[]};self.assertEqual(M.evaluate(d)['decision'],'ALLOW')
 def test_nan_or_bool_threshold_unknown(self):
  for bad in [float('nan'),True,-1]:
   d=sample();d['policy']['physical_gib']=bad;self.assertEqual(M.evaluate(d)['decision'],'UNKNOWN')

if __name__=='__main__':unittest.main()
