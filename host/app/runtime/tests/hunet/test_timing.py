import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/hunet'))
from summary import summarize
def sample(index,kind=3,value=None):return dict(integrity='PASS',scope='TEST_PROTOCOL_NOT_BOARD',run_id='ab'*16,boot_id='boot',generation=3,roi_index=0,mode='resident',source_digest='11'*32,sample_kind=kind,sample_index=index,T_resident_ns=index+1 if value is None else value)
class Timing(unittest.TestCase):
 def test_same_math_failure_or_missing_blocks_full_batch(self):
  batch=[sample(0,1)]+[sample(i,2) for i in range(3)]+[sample(i) for i in range(20)]
  for a in batch:
   a.update(scope='HW4_READONLY_PREFLIGHT',**{'Linux-v5_same_math':dict(status='PASS'),'R2_numeric':dict(status='FAIL'),'ORT_application_numeric':dict(status='FAIL')})
  self.assertTrue(summarize(batch,'resident')['publish_performance'])
  for value in [dict(status='FAIL'),dict(status='NO_FROZEN_REFERENCE'),None]:
   batch[4]['Linux-v5_same_math']=value
   out=summarize(batch,'resident');self.assertFalse(out['publish_performance'],out)
   self.assertIn('LINUX_V5_NUMERIC_NOT_PASS',out['errors'])

 def test_p95_and_no_warmup_in_stats(self):
  a=summarize([sample(0,1,1000)]+[sample(i,2,1000) for i in range(3)]+[sample(i) for i in range(20)],'resident');self.assertEqual(a['p95_ns'],19.05);self.assertEqual(a['median_ns'],10.5);self.assertFalse(a['publish_performance'])
 def test_duplicate_sample_rejected(self):self.assertFalse(summarize([sample(0),sample(0)],'resident')['publish_performance'])
 def test_earlier_bad_last_good(self):
  bad=sample(0);bad['integrity']='FAIL';self.assertFalse(summarize([bad,sample(1)],'resident')['publish_performance'])
 def test_missing_sample_and_changed_epoch(self):
  a=[sample(i) for i in range(20)];a[-1]['boot_id']='different';self.assertFalse(summarize(a,'resident')['publish_performance'])
if __name__=='__main__':unittest.main()
