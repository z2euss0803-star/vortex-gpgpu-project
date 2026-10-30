import pathlib
import tempfile
import unittest
from scripts.unified.generate_mosaic_package import generate,cooperative_workspace_policy
from scripts.unified.mosaic_program import selected_directory
class CoopPackagePolicyTests(unittest.TestCase):
 def test_policy_has_exact_bounded_work_and_original_schedule(self):
  for option in (0,1):
   self.assertEqual(cooperative_workspace_policy(option),dict(version=1,abi=3,layout_version=1,profile_wire_version=2,prefix_bytes=64,conv_raw_bytes=384,core_profile_bytes=256,schedule_version=1,weight_layout_version=1,profiling_options=option))
 def test_bad_policy_types_and_bits_refused(self):
  for option in (True,False,-1,2,3,1<<32,0.0,'1',None):
   with self.subTest(option=option),self.assertRaises(ValueError):cooperative_workspace_policy(option)
 def test_generator_rejects_implicit_or_mixed_family_before_reading_inputs(self):
  args=('missing-weights','missing-manifest','missing-input',12,8,16,'unused-output')
  for options in (dict(cooperative=True),dict(cooperative=True,pointwise_budget=16),dict(cooperative=1),dict(fusion=True,pointwise_budget=16,profiling_options=1),dict(fusion=True,pointwise_budget=16,cooperative=True,profiling_options=True)):
   with self.subTest(options=options),self.assertRaises(ValueError):generate(*args,**options)
 def test_selection_never_falls_back_to_old_published_family(self):
  with tempfile.TemporaryDirectory() as name:
   with self.assertRaisesRegex(ValueError,'INDEPENDENT_ACTUAL_RV32_PUBLICATION_REQUIRED mosaic-int16-v6'):selected_directory(pathlib.Path(name),fused=True,cooperative=True)
  with self.assertRaises(ValueError):selected_directory(pathlib.Path('.'),cooperative=True)
if __name__=='__main__':unittest.main(verbosity=2)
