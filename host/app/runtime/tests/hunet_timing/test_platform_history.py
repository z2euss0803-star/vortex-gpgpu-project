import importlib.util,json,pathlib,unittest
R=pathlib.Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('history_freeze',R/'scripts/hunet_hybrid/freeze.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class PlatformHistory(unittest.TestCase):
 def test_historical_role_contract_requires_its_own_source_bound_platform(self):
  old=json.loads((R/'contracts/baselines/hunet-v5-before-w4.json').read_bytes())
  c=m.freeze_contract(R);c['profile']=old['profile'];c['identities']=m.identities(old)
  with self.assertRaises(ValueError):m.verify_baseline(c)
  self.assertIsNone(m.verify_baseline(c,old))
  bad=dict(c,threads=2)
  with self.assertRaises(ValueError):m.verify_baseline(bad,old)
if __name__=='__main__':unittest.main()
