import copy,unittest
from scripts.mosaic.audit_quality import audit_cases

def fixture():
 rows=[];cases=[]
 for i in range(21):
  sha=('%064x'%(i+1));raw=[dict(branch='W16A16',index=j,bytes=4,sha256=sha) for j in range(16)]
  rows.append(dict(index=i,shape=[1,3,256,256],raw=raw))
  result=dict(compile_exit=0,natural_exit=0,thresholds_changed=False,default_changed=False,gpu_raw_cycles=None,input_archive_sha256=sha,layers=[dict(stage=j,max_abs=0,rmse=0,violations=0,different_bits=0,pass_=True,gpu_raw_cycles=None) for j in range(16)],outputs={'candidate-stage%02d.f32'%j:dict(bytes=4,sha256=sha) for j in range(16)})
  for layer in result['layers']:layer['pass']=layer.pop('pass_')
  cases.append(dict(row=i,expected_input_archive_sha256=sha,result=result))
 return dict(rows=rows),cases

class Audit(unittest.TestCase):
 def test_complete_exact_set(self):
  b,c=fixture();r=audit_cases(b,c);self.assertTrue(r['complete']);self.assertEqual(r['verified_layer_outputs'],336)
 def test_missing_case_never_complete(self):
  b,c=fixture()
  with self.assertRaises(ValueError):audit_cases(b,c[:-1])
  r=audit_cases(b,c[:-1],allow_partial=True);self.assertFalse(r['complete'])
 def test_duplicate_row_rejected(self):
  b,c=fixture();c[-1]=copy.deepcopy(c[0])
  with self.assertRaises(ValueError):audit_cases(b,c)
 def test_forged_success_does_not_hide_output_hash(self):
  b,c=fixture();c[4]['result']['outputs']['candidate-stage15.f32']['sha256']='f'*64
  with self.assertRaises(ValueError):audit_cases(b,c)
 def test_missing_layer_rejected(self):
  b,c=fixture();c[2]['result']['layers'].pop()
  with self.assertRaises(ValueError):audit_cases(b,c)
 def test_nonfinite_metric_not_a_pass(self):
  b,c=fixture();c[1]['result']['layers'][0]['rmse']=float('nan')
  with self.assertRaises(ValueError):audit_cases(b,c)
 def test_failed_exit_weak_threshold_and_fake_cycle_rejected(self):
  for field,value in [('natural_exit',1),('thresholds_changed',True),('gpu_raw_cycles',123)]:
   b,c=fixture();c[0]['result'][field]=value
   with self.assertRaises(ValueError):audit_cases(b,c)
 def test_wrong_frozen_input_archive_rejected(self):
  b,c=fixture();c[0]['result']['input_archive_sha256']='e'*64
  with self.assertRaises(ValueError):audit_cases(b,c)

if __name__=='__main__':unittest.main()
