import copy,importlib,json,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Timing(unittest.TestCase):
 def api(self):return importlib.import_module('scripts.s8.audit_run')
 def job(self):
  j=dict(elapsed_raw=1234,elapsed_u64=1234,elapsed_address=0xa0000028,elapsed_valid=1,pre_ack=1,clock=1,timing_invalid=0,valid_events=(1<<0)|(1<<3)|(1<<4)|(1<<8))
  for event,value in ((0,100),(3,200),(4,1000),(8,1400)):
   j.update({'event%d_first_ns'%event:value,'event%d_last_ns'%event:value,'event%d_calls'%event:1})
  return j
 def test_private_result_bits_match_approved_spec(self):
  bits=json.loads((ROOT/'contracts/s8/wire.json').read_text())['result_valid_bits']
  self.assertEqual(bits,dict(raw=1,decoded=2,recovery_raw=4,recovery_decoded=8,status=16,phase=32))
 def test_raw_and_exclusive_envelopes_nested_never_added(self):
  j=self.job();ops=[dict(clock=1,begin_ns=300,end_ns=900,metric=6)]
  r=self.api().audit_timing(j,ops);self.assertEqual(r['exclusive_ns'],[100,800,400]);self.assertIsNone(r['gpu_seconds']);self.assertEqual(r['elapsed_raw'],1234)
  self.assertEqual(r['nested_operations'][0]['ns'],600)
  self.assertEqual(r['residual_ns'],700)
  r=self.api().audit_timing(j,ops+ops);self.assertEqual(r['residual_ns'],700)
  for key,value in [('elapsed_raw',0),('elapsed_valid',0),('pre_ack',0),('clock',3),('elapsed_address',0xa0000024),('timing_invalid',1),('valid_events',0),('gpu_seconds',1.0)]:
   b=copy.deepcopy(j);b[key]=value
   with self.assertRaises(ValueError,msg=key):self.api().audit_timing(b,ops)
  with self.assertRaises(ValueError):self.api().audit_timing(j,[dict(clock=2,begin_ns=0,end_ns=10,metric=6)])
 def test_phase_wrap_and_unknown_reset(self):
  api=self.api();self.assertEqual(api.phase_deltas([2**44-3,2]),[5])
  for values in ([1,0],[0,2**44],[1,None]):
   with self.assertRaises(ValueError):api.phase_deltas(values)
if __name__=='__main__':unittest.main()
