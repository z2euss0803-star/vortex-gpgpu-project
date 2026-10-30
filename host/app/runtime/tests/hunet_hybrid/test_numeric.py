import copy,json,sys,unittest,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
import audit_run as aud
class NumericTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  path=(ROOT/'release/CURRENT').read_text().strip()
  if os.name=='nt':path=path.replace('/mnt/f/','F:/')
  rel=Path(path);cls.refs=json.loads((rel/'references.json').read_bytes())
  for k in ['original_acceptance','original_R2','original_ORT_FP32']:cls.refs[k]['path']=str(rel/cls.refs[k]['path'])
  for row in cls.refs['v5'].values():row['path']=str(rel/row['path'])
 def test_original_roi0_failure_unchanged(self):
  r=aud.old.numeric(Path(self.refs['v5']['0']['path']).read_bytes(),0,self.refs)
  self.assertEqual(r['R2_numeric']['status'],'PASS');self.assertEqual(r['ORT_application_numeric']['status'],'FAIL');self.assertEqual(r['ORT_application_numeric']['mismatch_count'],8081);self.assertEqual(r['ORT_application_numeric']['nrmse_limit'],0.0001)
 def test_changed_reference_or_acceptance_rejected(self):
  for role in ['original_acceptance','original_R2','original_ORT_FP32']:
   refs=copy.deepcopy(self.refs);refs[role]['sha256']='1'*64
   with self.assertRaisesRegex(ValueError,'FROZEN_REFERENCE_IDENTITY'):aud.old.checked_references(refs)
if __name__=='__main__':unittest.main()
