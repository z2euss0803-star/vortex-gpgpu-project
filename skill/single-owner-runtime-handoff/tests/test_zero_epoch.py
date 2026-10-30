import importlib.util,pathlib,unittest
P=pathlib.Path
S=importlib.util.spec_from_file_location('zero_gate',P(__file__).parents[1]/'scripts/check_handoff.py');M=importlib.util.module_from_spec(S);S.loader.exec_module(M)
class ZeroEpochTest(unittest.TestCase):
 def test_zero_epoch_is_valid_when_protocol_uses_it(self):
  owner=dict(boot_id='boot-a',owner_pid=1,owner_start_ticks=0,owner_epoch=0,current_mode='motor-control')
  d=dict(expected=owner,snapshot=dict(owner,owner_count=1,inflight=0,dma_pending=0,tex_pending=0,completion_pending=0,retirement_unknown=False,observed_at_ns=0),now_ns=0,max_age_ns=0,target_mode='diagnostic')
  self.assertEqual(M.evaluate(d)['decision'],'ALLOW_HANDOFF')
if __name__=='__main__':unittest.main()
