import importlib.util,pathlib,unittest
P=pathlib.Path(__file__).resolve().parents[2]/'scripts/hunet_fused/summary.py'
s=importlib.util.spec_from_file_location('closure_summary',P);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class HandoffClosure(unittest.TestCase):
 def values(self):
  gpu=[dict(elapsed_cycles=96969696,T_submit_wait_ns=1000,T_evidence_ns=10,T_reap_ns=10)]
  cpu=[dict(sequence=296,stage=8,T_begin_call_ns=100,T_compute_ns=20,T_commit_call_ns=200,T_evidence_call_ns=10,T_writeback_capture_ns=0,T_cpu_reference_ns=0)]
  meta=dict(T_resident_ns=2000,T_checkpoint_ns=30,T_diagnostic_capture_ns=0,T_final_read_ns=20,T_load_ns=100)
  detail=dict(version=1,T_roi_begin_call_ns=250,T_roi_end_call_ns=150,cpu=[dict(sequence=296,stage=8,T_handler_ns=480,T_checkpoint_ns=0,T_input_sha_ns=50,T_output_sha_ns=50,T_gelu_call_ns=40,T_gelu_exterior_ns=20)])
  meta['timing_extension']=detail
  return gpu,cpu,meta
 def test_full_cpu_and_roi_intervals_close_without_double_counting(self):
  g,c,r=self.values();v=m.timing_summary(g,c,r,96969696)
  self.assertEqual(v.get('T_CPU_handler_ns'),480);self.assertEqual(v.get('T_roi_begin_call_ns'),250)
  self.assertEqual(v.get('T_unclassified_ns'),50);self.assertEqual(v['T_CPU_outer_ns'],330)
  self.assertEqual(v.get('T_CPU_gelu_exterior_ns'),20)
 def test_overlapping_cpu_checkpoint_is_subtracted_once(self):
  g,c,r=self.values();r['timing_extension']['cpu'][0]['T_handler_ns']=490;r['timing_extension']['cpu'][0]['T_checkpoint_ns']=10
  self.assertEqual(m.timing_summary(g,c,r,96969696).get('T_unclassified_ns'),50)
 def test_malformed_extension_never_silently_falls_back_to_legacy(self):
  for field,value in [('T_handler_ns',1),('T_gelu_call_ns',1),('T_gelu_exterior_ns',999),('stage',9),('T_input_sha_ns',-1)]:
   g,c,r=self.values();r['timing_extension']['cpu'][0][field]=value
   with self.assertRaises(ValueError):m.timing_summary(g,c,r,96969696)
 def test_legacy_without_extension_retains_original_scope(self):
  g,c,r=self.values();del r['timing_extension'];v=m.timing_summary(g,c,r,96969696)
  self.assertEqual(v['T_CPU_outer_ns'],330);self.assertNotIn('T_CPU_handler_ns',v)
if __name__=='__main__':unittest.main()
