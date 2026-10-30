import hashlib,json,pathlib,unittest
R=pathlib.Path(__file__).resolve().parents[2]
class SelectedW4(unittest.TestCase):
 def test_candidate_profile_and_frozen_hardware_are_bound_to_requested_w4(self):
  p=R/'contracts/selected_hw4_w4_v1.json';self.assertTrue(p.is_file(),'requested W4 platform selection not implemented')
  s=json.loads(p.read_bytes());profile=json.loads((R/'contracts/profiles/hw4-hunet-v5.json').read_bytes());model=json.loads((R/'contracts/hunet_v5.json').read_bytes())
  self.assertEqual(s['schema'],1);self.assertEqual(profile['hardware_baseline_id'],'HUNET_HW4_AXI_W4_20261015_faeaddb98d5d')
  for k,want in [('bit_sha256','faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc'),('xsa_sha256','08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536')]:
   self.assertEqual(profile[k],want);self.assertEqual(model['profile'][k],want)
  roles={x['role']:x for x in model['files'] if x['role'] in ('bit','xsa')}
  for role in ('bit','xsa'):
   b=(R.parents[1]/roles[role]['path']).read_bytes();self.assertEqual(hashlib.sha256(b).hexdigest(),profile[role+'_sha256'])
  old=json.loads((R/'contracts/baselines/hw4-before-w4.json').read_bytes())
  self.assertEqual(old['bit_sha256'],'deb74c77e7e76e9108f49307371c42c889b21f8e764c545987a7786ce543ebc8')
  for k in ('control','cdma','ready_gpio','ps_base','ps_size','gpu_pl_base','gpu_pl_end','pl_system_base','topology','private','watchdog'):
   self.assertEqual(profile[k],old[k],k)
if __name__=='__main__':unittest.main()
