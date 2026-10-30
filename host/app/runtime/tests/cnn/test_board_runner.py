import os,subprocess,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SHELL='F:/git/Git/bin/bash.exe' if os.name=='nt' else 'bash'
class Runner(unittest.TestCase):
 def test_shell_syntax(self):
  p=ROOT/'scripts/cnn/run_board.sh';self.assertTrue(p.is_file(),'Board runner missing')
  self.assertEqual(subprocess.run([SHELL,'-n',str(p)],capture_output=True).returncode,0)
 def test_no_authorization_no_commands(self):
  p=ROOT/'scripts/cnn/run_board.sh';self.assertTrue(p.is_file(),'Board runner missing')
  env=os.environ.copy();env.pop('CNN_BOARD_EXECUTION_APPROVED',None)
  r=subprocess.run([SHELL,str(p)],env=env,capture_output=True)
  self.assertEqual(r.returncode,2);self.assertIn(b'NO_BOARD_AUTHORIZATION',r.stderr)
 def test_invalid_run_id_rejected_before_host_access(self):
  p=ROOT/'scripts/cnn/run_board.sh';self.assertTrue(p.is_file(),'Board runner missing')
  env=os.environ.copy();env['CNN_BOARD_EXECUTION_APPROVED']='COPY-VECADD-CNN-96-JOBS'
  r=subprocess.run([SHELL,str(p),'H0-LINUX-CNN-I32-V1-20261011T070112Z','a'*40,'12345678-1234-1234-1234-123456789abc','b'*64,'c'*64,'../unsafe'],env=env,capture_output=True)
  self.assertEqual(r.returncode,2);self.assertIn(b'INVALID_ARGUMENTS',r.stderr)
if __name__=='__main__':unittest.main()
