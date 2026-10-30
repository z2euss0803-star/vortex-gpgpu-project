import pathlib,unittest
from scripts.unified import mosaic_program as program
class VariantTests(unittest.TestCase):
 def test_explicit_variant_resolution_and_legacy_compatibility(self):
  resolve=getattr(program,'resolve_variant',None)
  self.assertTrue(callable(resolve),'EXPLICIT_VARIANT_RESOLVER_NOT_IMPLEMENTED')
  self.assertEqual(resolve(), 'reuse-v8')
  self.assertEqual(resolve(fused=False),'pack32-ro64-v3')
  self.assertEqual(resolve(fused=True),'fused-pointwise-v4')
  self.assertEqual(resolve(fused=True,cooperative=True),'cooperative-v6')
  for name in ('pack32-ro64-v3','fused-pointwise-v4','cooperative-v6','reuse-v7','reuse-v8'):self.assertEqual(resolve(name),name)
  for args in (dict(variant='unknown'),dict(variant=True),dict(variant='reuse-v7',fused=False),dict(variant='reuse-v7',cooperative=True),dict(variant='cooperative-v6',cooperative=False),dict(variant='pack32-ro64-v3',fused=True),dict(fused=1),dict(cooperative=1),dict(cooperative=True)):
   with self.subTest(args=args),self.assertRaises(ValueError):resolve(**args)
if __name__=='__main__':unittest.main(verbosity=2)
