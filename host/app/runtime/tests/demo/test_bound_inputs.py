import hashlib,json,pathlib,tempfile,unittest
from scripts.demo.build_bound import formal_input,checked_dependency
class Inputs(unittest.TestCase):
    def test_small_or_changed_input_and_dependency_refused(self):
        with tempfile.TemporaryDirectory() as d:
            d=pathlib.Path(d);p=d/'input.f32';p.write_bytes(b'abcd');h=hashlib.sha256(b'abcd').hexdigest()
            self.assertEqual(checked_dependency(p,h),p)
            with self.assertRaises(ValueError):checked_dependency(p,'01'*32)
            (d/'manifest.json').write_text(json.dumps(dict(row=1,shape=[1,3,256,256],files={'input.f32':dict(sha256=h)})))
            with self.assertRaises(ValueError):formal_input(p)
            (d/'manifest.json').write_text(json.dumps(dict(row=1,shape=[1,3,960,1280],files={'input.f32':dict(sha256=h)})))
            with self.assertRaises(ValueError):formal_input(p)
if __name__=='__main__':unittest.main()
