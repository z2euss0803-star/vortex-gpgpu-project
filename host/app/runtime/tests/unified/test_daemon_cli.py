"""Native binary configuration checks only; never opens a board GPU device."""
import hashlib,json,os,pathlib,subprocess,tempfile,unittest
BINARY=os.environ['VX_DAEMON_BINARY']
class NativeStartup(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='vx-native-cli-');self.root=pathlib.Path(self.temp.name);self.path=self.root/'config.json'
  self.config=dict(version=1,private_dir=str(self.root/'private'),evidence_dir=str(self.root/'journal'),device_path=str(self.root/'never-a-device'),backend_mode='compute-v2',legacy_profile=0,hardware_contract='01'*32,bit='02'*32,xsa='03'*32,compatible='04'*32,device_generation=1,provider_flags=1)
 def tearDown(self):self.temp.cleanup()
 def config_file(self,config=None,raw=None):
  self.path.write_text(raw if raw is not None else json.dumps(config or self.config));self.path.chmod(0o600)
 def run_cli(self,*args):
  r=subprocess.run([BINARY]+list(args),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=5)
  for marker in (b'ERROR: AddressSanitizer',b'WARNING: ThreadSanitizer',b'FATAL: ThreadSanitizer',b'LeakSanitizer',b'runtime error:'):
   self.assertNotIn(marker,r.stdout+r.stderr)
  return r
 def test_config_only_no_device_open(self):
  self.config_file();r=self.run_cli('--check-config',str(self.path));self.assertEqual(r.returncode,0,r.stderr.decode());j=json.loads(r.stdout);self.assertEqual(j['scope'],'OFFLINE_CONFIG_VALIDATION');self.assertIsNone(j['gpu_raw_cycles']);self.assertFalse(j['device_opened']);self.assertEqual(j['config_sha256'],hashlib.sha256(self.path.read_bytes()).hexdigest());self.assertEqual(j['backend_mode'],'compute-v2');self.assertFalse((self.root/'private').exists());self.assertFalse((self.root/'journal').exists())
 def test_strict_fields_and_json(self):
  for extra in ('scope','transport','raw_program','boot_id','board_identity_verified'):
   c=dict(self.config);c[extra]='fixture';self.config_file(c);self.assertNotEqual(self.run_cli('--check-config',str(self.path)).returncode,0)
  self.config_file(raw='{"version":1,"version":1}');self.assertNotEqual(self.run_cli('--check-config',str(self.path)).returncode,0)
 def test_explicit_modes_no_fallback(self):
  for mode,profile,provider,valid in [('auto',0,1,False),('compute-v2',6,1,False),('legacy-compat',6,1,True),('legacy-compat',4,1,True),('legacy-compat',5,1,False),('legacy-compat',6,2,False)]:
   c=dict(self.config);c.update(backend_mode=mode,legacy_profile=profile,provider_flags=provider);self.config_file(c);r=self.run_cli('--check-config',str(self.path));self.assertEqual(r.returncode==0,valid,(mode,profile,provider,r.stderr))
 def test_private_regular_config(self):
  self.config_file();self.path.chmod(0o644);self.assertNotEqual(self.run_cli('--check-config',str(self.path)).returncode,0);self.path.chmod(0o600)
  link=self.root/'symlink';link.symlink_to(self.path);self.assertNotEqual(self.run_cli('--check-config',str(link)).returncode,0)
  hard=self.root/'hard';os.link(str(self.path),str(hard));self.assertNotEqual(self.run_cli('--check-config',str(self.path)).returncode,0)
 def test_explicit_serve_and_no_fixture_option(self):
  self.assertEqual(self.run_cli().returncode,2);self.assertEqual(self.run_cli('--help').returncode,0);self.assertEqual(self.run_cli('--fixture').returncode,2)
  self.config_file();r=self.run_cli('--serve',str(self.path));self.assertNotEqual(r.returncode,0);self.assertIn(b'DEVICE',r.stderr);self.assertFalse((self.root/'private/control.sock').exists())
 def test_production_binary_excludes_host_core_fixture(self):
  r=subprocess.run(['nm','-C',BINARY],stdout=subprocess.PIPE,stderr=subprocess.PIPE);self.assertEqual(r.returncode,0);self.assertNotIn(b'backend_bridge_',r.stdout)
if __name__=='__main__':unittest.main()
