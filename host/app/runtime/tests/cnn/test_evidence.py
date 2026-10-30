"""Real subprocess failures and immutable gate output directories."""
import importlib.util
import pathlib
import sys
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('collector',ROOT/'scripts/collect_evidence.py')
collector=importlib.util.module_from_spec(spec);spec.loader.exec_module(collector)
class Evidence(unittest.TestCase):
    def test_complete_gate_inventory(self):
        self.assertEqual(len(getattr(collector,'STAGES',())),17)
        self.assertIn('cnn-session',collector.STAGES)
        self.assertIn('cnn-release',collector.STAGES)
    def test_failed_command_is_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=pathlib.Path(directory)
            r=collector.run('failure',[sys.executable,'-c','import sys;print("actual failure");sys.stderr.write("err");sys.exit(7)'],folder)
            self.assertEqual(r['exit'],7);self.assertEqual(r['status'],'FAIL')
            self.assertIn('ended',r);self.assertIn('stdout_sha256',r);self.assertIn('stderr_sha256',r)
            self.assertEqual((folder/'failure.log').read_text().strip(),'actual failure')
    def test_run_directory_cannot_be_reused(self):
        self.assertTrue(hasattr(collector,'new_folder'),'Immutable evidence directory missing')
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            folder=collector.new_folder(root,'unit-run')
            (folder/'preserved').write_text('original')
            with self.assertRaises(FileExistsError):collector.new_folder(root,'unit-run')
            with self.assertRaises(ValueError):collector.new_folder(root,'../outside')
            self.assertEqual((folder/'preserved').read_text(),'original')
if __name__=='__main__':unittest.main()
