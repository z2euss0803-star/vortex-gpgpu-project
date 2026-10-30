import json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
from summary import write_summary,summarize_formal
class TimingTests(unittest.TestCase):
 def test_separate_csv_and_no_unmeasured_seconds(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'summary';a=dict(integrity='PASS',publish_performance=False,stop_reason='ORT_FAIL',gpu_seconds=None,gpu=[dict(sequence=1,elapsed_cycles=42)],per_layer_cpu=[dict(sequence=296,user_cpu_ns=12)],logical=[dict(sequence=1,role='GPU')],stage_errors=[dict(stage=8,max_abs=0.0)])
   write_summary(a,out);self.assertTrue((out/'performance-not-accepted.txt').is_file());self.assertIn('elapsed_cycles',(out/'gpu-jobs.csv').read_text());self.assertNotIn('elapsed_cycles',(out/'cpu-stages.csv').read_text());self.assertIsNone(json.loads((out/'audit.json').read_bytes())['gpu_seconds']);self.assertFalse(json.loads((out/'stop-receipt.json').read_bytes())['formal_allowed'])
 def test_no_fabricated_formal_completion(self):
  with self.assertRaisesRegex(ValueError,'FORMAL_BATCH_NOT_ACCEPTED'):summarize_formal([])
if __name__=='__main__':unittest.main()
