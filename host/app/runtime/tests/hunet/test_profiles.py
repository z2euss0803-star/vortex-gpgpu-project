"""Catch wrong bit/XSA/source ancestry, stale SHA and invalid ROI inclusion."""
import copy, importlib.util, json, pathlib, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
(ROOT/'build/test-tmp').mkdir(parents=True,exist_ok=True)
VAULT = ROOT.parents[1]
SCRIPT = ROOT / 'scripts/hunet/freeze_inputs.py'
class ProfileTests(unittest.TestCase):
    def load(self):
        self.assertTrue(SCRIPT.is_file(), 'production freeze generator missing')
        spec = importlib.util.spec_from_file_location('freeze_hunet', str(SCRIPT))
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        return module
    def test_hw4_freeze(self):
        m = self.load()
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/test-tmp')) as d:
            pathlib.Path(d).resolve().relative_to(ROOT.resolve())
            f = m.freeze_inputs(VAULT, pathlib.Path(d))
            self.assertEqual(f['profile']['id'], 4)
            self.assertEqual(f['profile']['gpu_pl_end'], 0x100000000)
            self.assertEqual(f['profile']['bit_sha256'], 'deb74c77e7e76e9108f49307371c42c889b21f8e764c545987a7786ce543ebc8')
            self.assertEqual(f['profile']['xsa_sha256'], '7fdd711f0229ee01d369188d44852c0f85193aada485ac4b1998e2b4e3e32a91')
            self.assertEqual(f['delivery_count'], 285)
            self.assertEqual(f['graph']['jobs_bytes'], 130944)
            self.assertEqual(f['graph']['matrices_bytes'], 10048)
            self.assertEqual(len(f['inputs']['valid_roi_indices']), 121)
            self.assertEqual(f['inputs']['excluded_truncated'], [33,34,35,36,37,38])
            self.assertNotEqual(f['profile']['bit_sha256'], f['protected_parents']['h0_bit_sha256'])
            self.assertEqual(m.validate_freeze(f, VAULT), [])
    def test_stale_sha_rejected(self):
        m = self.load()
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/test-tmp')) as d:
            pathlib.Path(d).resolve().relative_to(ROOT.resolve())
            f = m.freeze_inputs(VAULT, pathlib.Path(d)); bad=copy.deepcopy(f)
            bad['files'][0]['sha256']='0'*64
            self.assertTrue(m.validate_freeze(bad,VAULT))
    def test_source_escape_rejected(self):
        m = self.load()
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/test-tmp')) as d:
            pathlib.Path(d).resolve().relative_to(ROOT.resolve())
            f=m.freeze_inputs(VAULT,pathlib.Path(d));f['files'][0]['path']='../../outside'
            self.assertTrue(m.validate_freeze(f,VAULT))
if __name__=='__main__': unittest.main()
