import json,pathlib,tempfile,unittest
from scripts.unified.generate_int16_packages import source_bytes
class CheckoutIdentity(unittest.TestCase):
 def test_working_text_cannot_bind_a_different_git_checkout(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);(root/'device').mkdir();p=root/'device/source.cpp';p.write_bytes(b'int x;\n');self.assertEqual(source_bytes(root,'device/source.cpp'),b'int x;\n')
   p.write_bytes(b'int x;\r\n')
   with self.assertRaisesRegex(ValueError,'SOURCE_CHECKOUT_LF'):source_bytes(root,'device/source.cpp')
 def test_immutable_vendor_bytes_are_never_normalized(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);(root/'vendor').mkdir();p=root/'vendor/frozen.c';p.write_bytes(b'original\r\n');self.assertEqual(source_bytes(root,'vendor/frozen.c'),b'original\r\n')
 def test_actual_bound_candidate_text_is_git_stable(self):
  root=pathlib.Path(__file__).resolve().parents[2];family=json.loads((root/'data/nnrt/programs/int16-family-v1/family.json').read_text())
  for row in family['source_rows']:
   if not row['path'].startswith('vendor/'):
    raw=source_bytes(root,row['path']);self.assertFalse(b'\r' in raw,row['path'])
if __name__=='__main__':unittest.main()
