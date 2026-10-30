"""Reject changed immutable inputs or silently overlapping identities."""
import hashlib,json,pathlib,tempfile,unittest
from scripts.image.freeze_inputs import freeze_inputs,reserve_namespace

class IdentityTests(unittest.TestCase):
    def test_frozen_content_identity_and_change_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'input';p.write_bytes(b'abc'); out=pathlib.Path(d)/'lock.json'
            lock=freeze_inputs([p],out)
            self.assertIn('files',lock)
            self.assertEqual(lock['files'][0]['sha256'],'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            before=out.read_bytes();p.write_bytes(b'abd')
            with self.assertRaises(ValueError):freeze_inputs([p],out)
            self.assertEqual(out.read_bytes(),before)
    def test_duplicate_and_non_regular_input_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'x';p.write_bytes(b'x')
            with self.assertRaises(ValueError):freeze_inputs([p,p],pathlib.Path(d)/'lock')
            with self.assertRaises(ValueError):freeze_inputs([pathlib.Path(d)],pathlib.Path(d)/'lock')
    def test_namespace_collision_rejected(self):
        candidate={'profile':256,'programs':list(range(512,522)),'ioctl_nrs':[112,113]}
        self.assertEqual(reserve_namespace(candidate,[{'profile':4,'programs':[9],'ioctl_nrs':[64]}]),candidate)
        for conflict in ({'profile':256},{'programs':[515]},{'ioctl_nrs':[112]}):
            with self.assertRaises(ValueError):reserve_namespace(candidate,[conflict])
    def test_invalid_candidate_namespace_rejected(self):
        for c in ({'profile':256,'programs':[512,512],'ioctl_nrs':[112]}, {'profile':256,'programs':[512],'ioctl_nrs':[-1]}):
            with self.assertRaises(ValueError):reserve_namespace(c,[])

if __name__=='__main__':unittest.main()
