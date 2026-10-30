import importlib.util,pathlib,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ImageTests(unittest.TestCase):
 def test_deployable_requires_rtl_cross_and_source_identity(self):
  s=importlib.util.spec_from_file_location('fused_image_test',ROOT/'scripts/hunet_fused/image.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
  for available in (0,12927053824,m.MIN_FREE-1):
   with self.assertRaises(ValueError):m.space_gate(available)
  self.assertEqual(m.space_gate(m.MIN_FREE),m.MIN_FREE)
  recipes=m.recipes('1'*40,'2'*32)
  self.assertIn('file://vxl_fused_release.h',recipes['kernel'])
  self.assertIn('cpu/gelu.elf ${D}${bindir}/vxl_hunet_fused',recipes['apps'])
  self.assertIn('INHIBIT_PACKAGE_STRIP = "1"',recipes['apps'])
  self.assertNotIn('vxl_hunet_hybrid',recipes['apps'])
  good=dict(binding='a',cpu='b',libm='c',library='f',program='d',boot='e',builtin=True,full_source=True,fit=True,dt=True,ssh=True)
  self.assertEqual(m.content_errors(good,good),[])
  for k in good:
   bad=dict(good);bad[k]=False if type(good[k]) is bool else 'wrong'
   self.assertTrue(m.content_errors(bad,good),k)
if __name__=='__main__':unittest.main()
