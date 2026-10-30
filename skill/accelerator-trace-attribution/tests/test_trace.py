import importlib.util,pathlib,unittest
P=pathlib.Path
S=importlib.util.spec_from_file_location('trace',P(__file__).parents[1]/'scripts/summarize_trace.py');M=importlib.util.module_from_spec(S);S.loader.exec_module(M)

def job(cycles=100,domain='gpu-a',hz=None):
 return dict(boot_id='b1',owner_epoch=1,run_id='run-a:input-a',request_id='r1',job_id='j1',clock_domain=domain,gpu_cycles=cycles,counter_hz=hz)
class TraceTests(unittest.TestCase):
 def test_receipt_copies_count_once(self):
  r=M.summarize({'jobs':[job(),job(),job()]});self.assertEqual(r['unique_jobs'],1);self.assertEqual(r['duplicate_receipts'],2);self.assertEqual(r['cycle_groups'][0]['cycles'],100)
 def test_conflicting_duplicate_rejected(self):self.assertEqual(M.summarize({'jobs':[job(100),job(101)]})['status'],'INVALID_INPUT')
 def test_clock_domains_not_added(self):
  r=M.summarize({'jobs':[job(domain='gpu-a'),job(domain='gpu-b')]});self.assertEqual(len(r['cycle_groups']),2);self.assertNotIn('total_cycles',r)
 def test_missing_frequency_no_seconds(self):self.assertIsNone(M.summarize({'jobs':[job()]})['cycle_groups'][0]['estimated_seconds'])
 def test_large_integer_and_frequency(self):
  r=M.summarize({'jobs':[job('9007199254740993',hz=100)]});self.assertEqual(r['cycle_groups'][0]['cycles'],9007199254740993);self.assertEqual(r['cycle_groups'][0]['estimated_seconds'],'90071992547409.93')
 def test_overlapping_intervals_use_union(self):
  a={'scope_id':'request-a','clock_domain':'mono-a','start_ns':0,'end_ns':10};b=dict(a,start_ns=5,end_ns=15)
  r=M.summarize({'jobs':[],'intervals':[a,b,a]});self.assertEqual(r['interval_groups'][0]['union_ns'],15);self.assertEqual(r['interval_groups'][0]['summed_ns'],30)
 def test_bad_counter_and_reverse_interval(self):
  for value in [-1,True,1.5,'NaN']:
   self.assertEqual(M.summarize({'jobs':[job(value)]})['status'],'INVALID_INPUT')
  d={'jobs':[],'intervals':[{'scope_id':'r','clock_domain':'m','start_ns':10,'end_ns':5}]};self.assertEqual(M.summarize(d)['status'],'INVALID_INPUT')
 def test_empty_and_missing_identity_rejected(self):
  self.assertEqual(M.summarize({'jobs':[]})['status'],'INVALID_INPUT')
  row=job();del row['boot_id'];self.assertEqual(M.summarize({'jobs':[row]})['status'],'INVALID_INPUT')

if __name__=='__main__':unittest.main()
