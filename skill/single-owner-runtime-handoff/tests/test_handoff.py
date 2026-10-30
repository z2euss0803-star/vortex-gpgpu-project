import importlib.util,pathlib,unittest,copy
P=pathlib.Path
S=importlib.util.spec_from_file_location('gate',P(__file__).parents[1]/'scripts/check_handoff.py');M=importlib.util.module_from_spec(S);S.loader.exec_module(M)

def sample():
 owner={'boot_id':'boot-a','owner_pid':42,'owner_start_ticks':123,'owner_epoch':7,'current_mode':'compute'}
 return {'expected':owner,'snapshot':dict(owner,owner_count=1,inflight=0,dma_pending=0,tex_pending=0,completion_pending=0,retirement_unknown=False,observed_at_ns=100),'now_ns':105,'max_age_ns':10,'target_mode':'render'}

class HandoffTests(unittest.TestCase):
 def test_drained_same_owner(self):self.assertEqual(M.evaluate(sample())['decision'],'ALLOW_HANDOFF')
 def test_timeout_without_drain_waits(self):
  d=sample();d['snapshot']['inflight']=1;d['snapshot']['wait_result']='TIMEOUT';self.assertEqual(M.evaluate(d)['decision'],'WAIT')
 def test_unproven_retirement_stops(self):
  d=sample();d['snapshot']['retirement_unknown']=True;self.assertEqual(M.evaluate(d)['decision'],'STOP')
 def test_pid_reuse_or_new_boot_stops(self):
  for field in ['boot_id','owner_start_ticks','owner_epoch']:
   d=sample();d['snapshot'][field]='changed';self.assertNotEqual(M.evaluate(d)['decision'],'ALLOW_HANDOFF')
 def test_stale_or_future_observation_unknown(self):
  for t in [94,106]:
   d=sample();d['snapshot']['observed_at_ns']=t;self.assertEqual(M.evaluate(d)['decision'],'UNKNOWN')
 def test_multiple_owners_stop(self):
  d=sample();d['snapshot']['owner_count']=2;self.assertEqual(M.evaluate(d)['decision'],'STOP')
 def test_missing_pending_cannot_be_zero(self):
  d=sample();del d['snapshot']['dma_pending'];self.assertEqual(M.evaluate(d)['decision'],'UNKNOWN')
 def test_bool_not_counter(self):
  d=sample();d['snapshot']['inflight']=False;self.assertEqual(M.evaluate(d)['decision'],'UNKNOWN')

if __name__=='__main__':unittest.main()
