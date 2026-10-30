import pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet_fused'))
from summary import timing_summary
class SummaryTests(unittest.TestCase):
 def test_clock_is_conditional_and_nested_intervals_are_not_summed(self):
  r=timing_summary([dict(elapsed_cycles=96969696,T_submit_wait_ns=2000000000,T_evidence_ns=1,T_reap_ns=2)],[],dict(T_load_ns=5,T_resident_ns=3000000000,T_checkpoint_ns=2,T_diagnostic_capture_ns=3,T_final_read_ns=4),96969696)
  self.assertEqual(r['gpu_seconds_conditional'],1);self.assertIsNone(r['gpu_seconds_physical'])
  self.assertEqual(r['T_submit_wait_ns'],2000000000);self.assertFalse(r['nested_intervals_additive'])
  with self.assertRaises(ValueError):timing_summary([dict(elapsed_cycles=2**32,submit_wait_ns=2,evidence_ns=1,reap_ns=2)],[],dict(T_resident_ns=10),96969696)
  with self.assertRaises(ValueError):timing_summary([],[],{},100000000)
if __name__=='__main__':unittest.main()
