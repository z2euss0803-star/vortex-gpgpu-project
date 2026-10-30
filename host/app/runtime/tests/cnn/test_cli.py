import subprocess
import os
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Cli(unittest.TestCase):
    def test_read_only_identity_mode_rejects_wrong_expected_source(self):
        binary=ROOT/os.environ.get('CNN_OUT','build/cnn')/'vxl_cnn'
        result=subprocess.run([str(binary),'--check-identity','0'*40,'H0-LINUX-CNN-I32-V1-20261011T070112Z','12345678-1234-1234-1234-123456789abc'],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn('no device opened',result.stderr)
        self.assertNotIn('REAL_DRIVER_CNN',result.stdout)
    def test_no_mock_or_unknown_argument(self):
        self.assertTrue((ROOT/'tools/vxl_cnn.cpp').is_file(),'Real-driver CLI missing')
        binary=ROOT/os.environ.get('CNN_OUT','build/cnn')/'vxl_cnn'
        for args in ([],['--mock'],['--evidence-dir'],['--evidence-dir','/tmp/a','--mock']):
            result=subprocess.run([str(binary)]+args,capture_output=True,text=True)
            self.assertEqual(result.returncode,2)
        env=os.environ.copy();env['VXL_TRANSPORT']='mock'
        result=subprocess.run([str(binary),'--evidence-dir','/tmp/should-not-be-created'],env=env,capture_output=True,text=True)
        self.assertEqual(result.returncode,2)
if __name__=='__main__':unittest.main()
