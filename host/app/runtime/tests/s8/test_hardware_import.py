import tempfile,unittest
from pathlib import Path
from scripts.s8 import hardware_import as api
class HardwareImport(unittest.TestCase):
 def test_native_exit_and_manifest_are_required(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);p.joinpath('raw').write_bytes(b'test-only')
   good={'raw':dict(bytes=9,sha256=api.sha(p/'raw'))}
   self.assertTrue(api.check_manifest(p,good))
   with self.assertRaises(ValueError):api.check_manifest(p,{'../raw':good['raw']})
   with self.assertRaises(ValueError):api.check_manifest(p,{'raw':dict(bytes=8,sha256=good['raw']['sha256'])})
   for bad in ({'exit_code':1},{'exit_code':0,'event':'started'},{'exit_code':False,'event':'exited'}):
    with self.assertRaises(ValueError):api.check_native(bad)
   self.assertTrue(api.check_native(dict(exit_code=0,event='exited',deadline_exceeded=False)))
if __name__=='__main__':unittest.main()
