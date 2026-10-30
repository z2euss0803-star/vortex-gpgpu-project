import importlib.util,pathlib,unittest
P=pathlib.Path
S=importlib.util.spec_from_file_location('evidence',P(__file__).parents[1]/'scripts/summarize_evidence.py');M=importlib.util.module_from_spec(S);S.loader.exec_module(M)

def record(layer='HOST',status='PASS',identity='v2',evidence='host.log'):
 return {'layer':layer,'status':status,'identity':identity,'evidence':evidence}

class EvidenceTests(unittest.TestCase):
 def test_host_does_not_prove_board(self):
  r=M.summarize({'target_identity':'v2','required_layers':['HOST','BOARD'],'records':[record()]})
  self.assertFalse(r['all_required_pass']);self.assertEqual(r['layers']['BOARD']['status'],'UNKNOWN')
 def test_old_board_pass_not_inherited(self):
  r=M.summarize({'target_identity':'v2','required_layers':['BOARD'],'records':[record('BOARD',identity='v1')]})
  self.assertFalse(r['all_required_pass']);self.assertEqual(len(r['other_identity_records']),1)
 def test_exit0_is_not_function_pass(self):
  r=M.summarize({'target_identity':'v2','required_layers':['SIMULATION'],'records':[record('SIMULATION','TOOL_COMPLETE')]})
  self.assertFalse(r['all_required_pass']);self.assertEqual(r['layers']['SIMULATION']['status'],'INCOMPLETE')
 def test_same_identity_failure_not_erased(self):
  r=M.summarize({'target_identity':'v2','required_layers':['BOARD'],'records':[record('BOARD'),record('BOARD','FAIL')]})
  self.assertEqual(r['layers']['BOARD']['status'],'CONFLICT');self.assertFalse(r['all_required_pass'])
 def test_missing_evidence_and_empty_requirements_rejected(self):
  for d in [{'target_identity':'v2','required_layers':[],'records':[]},{'target_identity':'v2','required_layers':['HOST'],'records':[record(evidence='')]}]:
   with self.subTest(d=d):self.assertEqual(M.summarize(d)['status'],'INVALID_INPUT')
 def test_genuine_required_layers(self):
  r=M.summarize({'target_identity':'v2','required_layers':['HOST','CROSS'],'records':[record(),record('CROSS')]})
  self.assertTrue(r['all_required_pass']);self.assertEqual(r['scope'],'DECLARED_RECORDS_ONLY')

if __name__=='__main__':unittest.main()
