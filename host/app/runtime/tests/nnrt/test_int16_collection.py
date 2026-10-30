import os,pathlib,tempfile,unittest
from scripts.unified.check_u11 import artifact
class Collection(unittest.TestCase):
 def test_fifo_is_metadata_not_opened(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'negative-fifo';os.mkfifo(str(p));r=artifact(p);self.assertEqual(r['kind'],'fifo');self.assertNotIn('sha256',r)
 def test_symlink_is_not_followed(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'negative-link';p.symlink_to('missing');r=artifact(p);self.assertEqual(r['kind'],'symlink');self.assertEqual(r['link'],'missing');self.assertNotIn('sha256',r)
 def test_regular_hash(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)/'bytes';p.write_bytes(b'');r=artifact(p);self.assertEqual(r['sha256'],'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');self.assertEqual(r['kind'],'file')
if __name__=='__main__':unittest.main()
