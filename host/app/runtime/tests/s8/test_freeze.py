import importlib,unittest,tempfile,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Freeze(unittest.TestCase):
 def test_freeze_rejects_dirty_generated_and_wrong_parent(self):
  api=importlib.import_module('scripts.s8.freeze');inventory=api.inventory(ROOT);paths=[r['path'] for r in inventory]
  self.assertIn('device/s8/main.cpp',paths);self.assertIn('core/s8_result.c',paths);self.assertIn('tools/vxl_s8.cpp',paths)
  self.assertIn('runtime/zynq/vortex.cpp',paths)
  for bad in ('../escape.c','evidence/source.c','build/s8/generated/vxl_s8_binding.h','include/vxl_s8_binding.h'):
   with self.assertRaises(ValueError):api.validate_paths(ROOT,paths+[bad])
  with self.assertRaises(ValueError):api.validate_paths(ROOT,[p for p in paths if p!='core/s8_result.c'])
  for parent,ancestor,dirty in [('0'*40,True,[]),(api.PARENT,False,[]),(api.PARENT,True,['core/s8_result.c'])]:
   with self.assertRaises(ValueError):api.check_identity(parent,ancestor,dirty)
  self.assertTrue(api.check_identity(api.PARENT,True,[]))
 def test_offline_target_has_no_board_or_rtl_action(self):
  text=(ROOT/'Makefile.s8').read_text();self.assertIn('test-s8-offline:',text)
  line=next(s for s in text.splitlines() if s.startswith('test-s8-offline:'))
  for bad in ('rtl','vivado','petalinux-build','board','jtag','sd-write'):self.assertNotIn(bad,line)
if __name__=='__main__':unittest.main()
