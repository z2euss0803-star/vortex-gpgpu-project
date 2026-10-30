import json,unittest
from pathlib import Path
from scripts.s8.catalog import load_catalog
ROOT=Path(__file__).resolve().parents[2]
class Compat(unittest.TestCase):
 def test_approved_tex_modes_are_separate_jobs(self):
  rows=load_catalog(ROOT/'contracts/s8/cases.json')['cases'];self.assertEqual(len(rows),35)
  cases={r['id']:r for r in rows}
  self.assertEqual((cases[4]['rule'],cases[4]['b_bytes'],cases[4]['result_bytes']),('tex_bilinear',40,20))
  self.assertEqual((cases[5]['rule'],cases[5]['a_bytes'],cases[5]['b_bytes'],cases[5]['result_bytes']),('tex_nearest',16,32,16))
  self.assertFalse(cases[5]['phase_allowed'])
 def test_native_compat_uses_frozen_wmma_and_single_lane_tex(self):
  text=(ROOT/'device/s8/compat.cpp').read_text()
  self.assertIn('WMMA::mma_sync',text);self.assertIn('vx_tex',text)
  self.assertIn('vx_tmc(1)',text);self.assertIn('vx_tmc(15)',text)
  self.assertIn('WMMA::tileM==8',text)
if __name__=='__main__':unittest.main()
