import unittest
from scripts.unified.mosaic_program import resolve_requested_variant
from scripts.unified.image import mosaic_payload_policy
from scripts.unified.bind_kernel import firmware_inputs
class FirmwareVariantTests(unittest.TestCase):
 def test_environment_and_explicit_variant(self):
  self.assertEqual(resolve_requested_variant({}), 'reuse-v8')
  self.assertEqual(resolve_requested_variant({'mosaic_variant':'reuse-v7'}), 'reuse-v7')
  self.assertEqual(resolve_requested_variant({},variant='reuse-v7'), 'reuse-v7')
  for env,kwargs in [({'mosaic_variant':'reuse-v7'},dict(variant='cooperative-v6')),({'mosaic_variant':'reuse-v7'},dict(fused=False)),({'mosaic_variant':'invalid'},{}),({'mosaic_fused':1},{}),({'mosaic_fused':True},dict(fused=False))]:
   with self.subTest(env=env,kwargs=kwargs),self.assertRaises(ValueError):resolve_requested_variant(env,**kwargs)
 def test_payload_policy_same_family(self):
  self.assertEqual(mosaic_payload_policy({}),dict(variant='reuse-v8',fusion=True,pointwise_budget=4096,profiling_options=0))
  self.assertEqual(mosaic_payload_policy({'mosaic_variant':'reuse-v7','mosaic_profiling_options':1}),dict(variant='reuse-v7',fusion=True,pointwise_budget=4096,profiling_options=1))
  for env in [{'mosaic_variant':'reuse-v7','mosaic_profiling_options':2},{'mosaic_variant':'reuse-v7','mosaic_pointwise_budget':0},{'mosaic_variant':'pack32-ro64-v3','mosaic_profiling_options':1},{'mosaic_variant':'reuse-v7','mosaic_pointwise_budget':True}]:
   with self.subTest(env=env),self.assertRaises(ValueError):mosaic_payload_policy(env)
 def test_conflicting_legacy_bool_refused_before_filesystem(self):
  with self.assertRaises(ValueError):firmware_inputs('/path/does/not/exist',4,mosaic_variant='reuse-v7',mosaic_fused=False)
if __name__=='__main__':unittest.main(verbosity=2)
