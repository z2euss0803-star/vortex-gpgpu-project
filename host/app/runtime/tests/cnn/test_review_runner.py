"""Exercise actual shell control flow using only temporary fake host/device paths."""
import hashlib,json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/cnn'))
from image_audit import identity
SHELL='F:/git/Git/bin/bash.exe' if os.name=='nt' else 'bash'
class RunnerReview(unittest.TestCase):
 def run_fake(self,folder,fail='',commit=None):
  base='/'+folder.drive[0].lower()+folder.as_posix()[2:] if os.name=='nt' else folder.as_posix()
  source='e5f55b2baa6ff04763658862c9563cfa2e34aa2f';build='H0-LINUX-CNN-I32-V1-20261011T070112Z';boot='12345678-1234-1234-1234-123456789abc'
  for p in ('etc','proc/sys/kernel/random','bin','runs'):(folder/p).mkdir(parents=True,exist_ok=True)
  manifest=json.dumps(identity(source,build),indent=2).encode()
  (folder/'etc/codex-build-id').write_text(build);(folder/'etc/codex-cnn-manifest.json').write_bytes(manifest)
  (folder/'proc/sys/kernel/random/boot_id').write_text(boot);(folder/'proc/cmdline').write_text('root=/dev/ram0 rw')
  def executable(name,text):
   p=folder/'bin'/name;p.write_text('#!/bin/sh\n'+text);p.chmod(0o700)
  executable('uname','case "$1" in -m) echo aarch64;; -r) echo 5.4.0-xilinx-v2020.1;; *) echo test-aarch64;; esac\n')
  executable('dmesg','echo OFFLINE_FAKE_KERNEL_DIAGNOSTIC\n')
  if fail=='hash':
   executable('sha256sum',"case \"$PWD\" in */runs/cnn-board-*) echo HASH_FAILURE >&2; exit 19;; esac\nexec /usr/bin/sha256sum \"$@\"\n")
  executable('vxl_info',f'''n=$(cat '{base}/info-count' 2>/dev/null || echo 0)
n=$((n+1)); echo "$n" > '{base}/info-count'
name=info; [ "$n" = 1 ] || name=final-info
echo "$name" >> '{base}/calls'
[ "$FAIL_STAGE" != "$name" ] || exit 17
echo '{{"state":1,"used":0}}'
''')
  for name,stage in (('vxl_copy_test','copy'),('vxl_vecadd','vecadd')):
   executable(name,f'echo {stage} >> "{base}/calls"\n[ "$FAIL_STAGE" != {stage} ] || exit 17\necho FAKE_TEST_PASS\n')
  executable('vxl_cnn',f'''if [ "$1" = --check-identity ]; then
 echo identity >> '{base}/calls'
 [ "$2" = {source} ] || {{ echo 'wrong expected source; no device opened' >&2; exit 1; }}
 exit 0
fi
echo cnn >> '{base}/calls'
[ "$FAIL_STAGE" != cnn ] || exit 17
mkdir "$2"; echo FAKE_CNN > "$2/run.json"
''')
  script=(ROOT/'scripts/cnn/run_board.sh').read_text()
  script=script.replace('/usr/bin/vxl_',base+'/bin/vxl_').replace('/etc/',base+'/etc/').replace('/proc/',base+'/proc/').replace('/tmp/cnn-board-',base+'/runs/cnn-board-')
  script="export PATH='"+base+"/bin':/usr/bin:/bin:\"$PATH\"\n"+script
  path=folder/'runner.sh';path.write_text(script)
  env=os.environ.copy();env['PATH']=str(folder/'bin')+os.pathsep+env['PATH'];env['CNN_BOARD_EXECUTION_APPROVED']='COPY-VECADD-CNN-96-JOBS';env['FAIL_STAGE']=fail
  args=[build,commit or source,boot,hashlib.sha256(manifest).hexdigest(),hashlib.sha256((folder/'bin/vxl_cnn').read_bytes()).hexdigest(),'offline']
  result=subprocess.run([SHELL,str(path)]+args,env=env,capture_output=True)
  return result,folder/'runs/cnn-board-offline'
 def test_wrong_commit_zero_device_tests(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);r,out=self.run_fake(p,commit='0'*40)
   self.assertEqual(r.returncode,1);calls=(p/'calls').read_text() if (p/'calls').exists() else ''
   self.assertNotIn('info',calls);self.assertNotIn('copy',calls);self.assertNotIn('cnn\n',calls)
 def test_hash_failure_refuses_success(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);r,out=self.run_fake(p,'hash')
   self.assertNotEqual(r.returncode,0,r.stdout)
   self.assertEqual((out/'runner.exit').read_text().strip(),'1')
   self.assertIn('HASH_FAILURE',(out/'hash-collection.stderr').read_text())
 def test_all_failure_stages_finalize_evidence(self):
  for stage in ('info','copy','vecadd','cnn','final-info'):
   with self.subTest(stage=stage),tempfile.TemporaryDirectory() as d:
    p=Path(d);r,out=self.run_fake(p,stage)
    self.assertEqual(r.returncode,17,(stage,r.stderr))
    self.assertEqual((out/'dmesg-after.txt').read_text().strip(),'OFFLINE_FAKE_KERNEL_DIAGNOSTIC')
    self.assertTrue((out/'SHA256SUMS').is_file());self.assertEqual((out/'runner.exit').read_text().strip(),'17')
    self.assertTrue((out/'finalize.log').is_file());self.assertEqual((out/(stage+'.exit')).read_text().strip(),'17')
if __name__=='__main__':unittest.main()
