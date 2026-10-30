"""Execute real network script against an explicit HOST ip shim in Ubuntu."""
import json,os,pathlib,subprocess,tempfile,unittest,re,copy
from scripts.unified.image import image_continuation,image_policy
ROOT=pathlib.Path(__file__).resolve().parents[2]
class NativeRuntimeAndImageContinuation(unittest.TestCase):
 def test_native_owner_and_python_control_client_declare_real_target_packages(self):
  recipe=(ROOT/'petalinux/unified/runtime.bb').read_text();deps=re.search(r'RDEPENDS_\$\{PN\}\s*\+=\s*"([^"]+)"',recipe).group(1).split()
  self.assertIn('libdrm',deps,'the production native KMS owner has a real libdrm runtime dependency')
  self.assertTrue({'libstdc++','libgcc'}<=set(deps));self.assertNotIn('python3-subprocess',deps,'no such standalone package in actual Yocto3.7.6 manifest; target Python client uses declared core/io/json/misc')
 def fixture(self):
  source=dict(source_snapshot_id='explicit-frozen-source');original=dict(scope='ACTUAL_COMPLETE_PROVIDER_BOOT_CHAIN_BUILD_NOT_BOARD',natural_exit=1,source_before=source,project='/explicit-private-project',commands=[dict(natural_exit=0,argv=['bash','-c','petalinux-config --silentconfig']),dict(natural_exit=255,argv=['bash','-c','petalinux-build'])])
  proof=dict(scope='ACTUAL_UNCHANGED_SOURCE_AFTER_FIRST_IMAGE_DEPENDENCY_PREFLIGHT_ERROR',natural_exit=0,snapshot=source,project=original['project'],project_metadata=[dict(path='/explicit-private-project/project-spec/configs/config',sha256='1'*64,bytes=1)])
  return original,proof,"ERROR: Nothing RPROVIDES 'python3-subprocess'\nERROR: Required build target has no buildable providers.\n"
 def test_only_known_dependency_preflight_with_unchanged_source_can_continue(self):
  original,proof,log=self.fixture();self.assertTrue(image_continuation(original,proof,log))
  variants=[]
  for index in (0,1):
   bad=copy.deepcopy(original);bad['commands'][index]['natural_exit']=True;variants.append((bad,proof,log))
  bad=dict(proof,snapshot=dict(source_snapshot_id='changed'));variants.append((original,bad,log));variants.append((original,dict(proof,natural_exit=1),log));variants.append((original,dict(proof,project='/another-project'),log));variants.append((dict(original,natural_exit=0),proof,log));variants.append((original,proof,'ERROR: compiler failure'));variants.append((original,proof,log+'NOTE: Executing Tasks\n'));variants.append((original,proof,log+'NOTE: Running task 1 of 10\n'))
  for args in variants:
   with self.subTest(args=args):self.assertFalse(image_continuation(*args),'never promote another failure or changed source')
 def test_authorized_continuation_preserves_exact_existing_startup_policy(self):
  policy='python unified_startup_policy() {\n    preserved_body\n}\nROOTFS_POSTPROCESS_COMMAND += " unified_startup_policy; "\n';parent='ROOTFS_POSTPROCESS_COMMAND += " old_parent_hook; "\n';previous=parent+'\n'+policy
  try:result=image_policy(previous,policy,True)
  except ValueError:result=None
  self.assertEqual(result,previous,'a verified continuation must not duplicate or drop prior startup hooks')
  self.assertEqual(image_policy(parent,policy),previous)
  for text,allowed in [(previous,False),(previous.replace('preserved_body','changed_body'),True),(previous+policy,True)]:
   with self.subTest(text=text,allowed=allowed),self.assertRaises(ValueError):image_policy(text,policy,allowed)

@unittest.skipUnless(os.name=='posix','actual POSIX shell checks require Ubuntu')
class StartupScripts(unittest.TestCase):
 def run_network(self,fail=None):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);root=pathlib.Path(temp.name)
  ip=root/'ip';ip.write_text('''#!/usr/bin/env python3
import os,json,pathlib,sys
p=pathlib.Path(os.environ['HOST_IP_STATE']);state=json.loads(p.read_text());a=sys.argv[1:];state['calls'].append(a)
if a==['link','set','dev','eth0','up']:
 code=state.get('fail_link',0)
elif a==['-4','addr','show','dev','eth0']:
 print('    inet 192.168.13.10/24 scope global eth0' if state['present'] else '');code=0
elif a==['addr','add','192.168.13.10/24','dev','eth0']:
 code=state.get('fail_add',0)
 if not code:state['present']=True
else:code=72
p.write_text(json.dumps(state));sys.exit(code)
''');ip.chmod(0o755);state=root/'state.json';doc=dict(calls=[],present=False)
  if fail:doc.update(fail)
  state.write_text(json.dumps(doc));env=dict(os.environ,PATH=str(root)+':'+os.environ['PATH'],HOST_IP_STATE=str(state))
  return root,state,env
 def test_first_add_then_idempotent_second_start_uses_no_replace(self):
  _,state,env=self.run_network();script=ROOT/'petalinux/unified/network-init'
  for _ in range(2):self.assertEqual(subprocess.run(['/bin/sh',str(script),'start'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode,0)
  got=json.loads(state.read_text());self.assertTrue(got['present']);self.assertEqual(got['calls'].count(['addr','add','192.168.13.10/24','dev','eth0']),1);self.assertFalse(any('replace' in row for row in got['calls']))
 def test_link_and_addr_errors_are_not_suppressed_or_retried(self):
  for fail,code in [({'fail_link':77},77),({'fail_add':69},69)]:
   with self.subTest(fail=fail):
    _,state,env=self.run_network(fail);p=subprocess.run(['/bin/sh',str(ROOT/'petalinux/unified/network-init'),'start'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);self.assertEqual(p.returncode,code);got=json.loads(state.read_text());self.assertFalse(got['present']);self.assertLessEqual(len(got['calls']),3)
 def test_service_shell_syntax_and_existing_supported_entry_points(self):
  p=ROOT/'petalinux/unified/service-init';self.assertEqual(subprocess.run(['/bin/sh','-n',str(p)],stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode,0)
  raw=p.read_text();self.assertIn('vxgpud --check-config /etc/vxgpud/config.json',raw);self.assertIn('vxgpud --serve /etc/vxgpud/config.json',raw);self.assertIn('control.sock models',raw);self.assertNotIn('control.sock shutdown',raw);self.assertNotIn('killall',raw);self.assertNotIn('SIGKILL',raw.replace('never SIGKILL',''))
if __name__=='__main__':unittest.main()
