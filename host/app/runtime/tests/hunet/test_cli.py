import os,subprocess,unittest
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/os.environ.get('HUNET_OUT','build')
class CLI(unittest.TestCase):
 def test_no_default_or_mock_compute(self):
  for name in ['vxl_hunet','vxl_hunet_copy','vxl_hunet_wmma']:
   for args in [[],['--mock'],['--run-id','bad']]:
    r=subprocess.run([str(out/name)]+args,capture_output=True);self.assertNotEqual(r.returncode,0)
 def test_help_no_device(self):
  for name in ['vxl_hunet','vxl_hunet_copy','vxl_hunet_wmma']:
   r=subprocess.run([str(out/name),'--help'],capture_output=True);self.assertEqual(r.returncode,0);self.assertIn(b'NO_DEFAULT_COMPUTE',r.stdout)
if __name__=='__main__':unittest.main()
