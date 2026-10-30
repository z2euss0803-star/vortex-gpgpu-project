import pathlib,re,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Endpoint(unittest.TestCase):
    def test_viewer_start_endpoint_matches_both_production_users(self):
        wrapper=(ROOT/'petalinux/demo/demo-viewer-init').read_text()
        provider=(ROOT/'demo/release_bound.cpp').read_text()
        runtime=(ROOT/'adapters/dual_runtime.cpp').read_text()
        endpoint=re.search(r'viewer\) exec .*? --serve \S+ (\S+)',wrapper).group(1)
        self.assertIn('"'+endpoint+'"',provider);self.assertIn('"'+endpoint+'"',runtime)
