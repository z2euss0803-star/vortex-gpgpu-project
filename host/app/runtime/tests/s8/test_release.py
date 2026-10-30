import importlib,tempfile,unittest,zipfile
from pathlib import Path
class Release(unittest.TestCase):
 def test_cross_source_identity_cannot_be_mixed_or_missing(self):
  api=importlib.import_module('scripts.s8.package_release')
  self.assertTrue(api.match_source('a'*64,[dict(source_sha256='a'*64)]*3))
  for bad in ([{}],[dict(source_sha256='b'*64)]):
   with self.assertRaises(ValueError):api.match_source('a'*64,bad)
 def test_missing_hardware_is_wait_not_old_substitution(self):
  api=importlib.import_module('scripts.s8.package_release')
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.assertEqual(api.hardware_gate({})['state'],'WAIT_HW_EXPORT')
   with self.assertRaises(ValueError):api.hardware_gate(dict(source_sha256='a'*64))
   bit=root/'new.bit';bit.write_bytes(b'TEST_ONLY BIT')
   xsa=root/'new.xsa'
   with zipfile.ZipFile(str(xsa),'w') as z:z.writestr('test.bit',b'DIFFERENT')
   with self.assertRaises(ValueError):api.embedded_bit_matches(xsa,bit)
if __name__=='__main__':unittest.main()
