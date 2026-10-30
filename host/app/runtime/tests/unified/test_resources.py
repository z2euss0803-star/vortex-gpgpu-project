"""Policy mutations and real temporary POSIX cache/archive operations only."""
import os,pathlib,json,hashlib,stat,tarfile,tempfile,unittest,importlib.util,sys
MODULES=pathlib.Path(os.environ.get('VX_U21_MODULE_DIR',str(pathlib.Path(__file__).resolve().parents[2]/'scripts/unified')))
def module(name):
 spec=importlib.util.spec_from_file_location('vx_u21_'+name,str(MODULES/(name+'.py')));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
resources=module('resources');archive=module('archive');G=1024**3
class Resources(unittest.TestCase):
 def env(self):return dict(host='z2euss-virtual-machine',account='z2euss',source_identity_verified=True,locks_owned=True,other_heavy=[],available_ram=16*G,free_disk=15*G,worker_count=1)
 def test_image_threshold_and_single_worker(self):
  e=self.env();g=resources.resource_gate(e,'image');self.assertEqual(g.get('required_disk'),16106127360);self.assertEqual(g['worker_count'],1);e['free_disk']-=1
  with self.assertRaises(ValueError):resources.resource_gate(e,'image')
  e=self.env();e['worker_count']=2
  with self.assertRaises(ValueError):resources.resource_gate(e,'image')
 def test_busy_C_and_unknown_identity_refused(self):
  for key,value in (('locks_owned',False),('other_heavy',['C compile']),('host','other'),('source_identity_verified',False)):
   e=self.env();e[key]=value
   with self.assertRaises(ValueError):resources.resource_gate(e,'unit')
 def test_parallel_worker_ram_is_not_multiplied_budget(self):
  e=self.env();e['worker_count']=2;g=resources.resource_gate(e,'cross');self.assertEqual(g.get('required_ram'),8*G);self.assertEqual(g['compiler_jobs'],2);self.assertEqual(g['test_runner_jobs'],1);e['available_ram']=8*G-1
  with self.assertRaises(ValueError):resources.resource_gate(e,'cross')
class Archives(unittest.TestCase):
 def setUp(self):
  parent=pathlib.Path('build/u21-test-temporary');parent.mkdir(parents=True,exist_ok=True);self.temp=tempfile.TemporaryDirectory(prefix='own-',dir=str(parent));self.base=pathlib.Path(self.temp.name).resolve();self.assertEqual(os.path.commonpath([str(self.base),str(parent.resolve())]),str(parent.resolve()));self.cache=self.base/'exclusive'/'sstate-cache';self.cache.mkdir(parents=True);(self.cache/'sub').mkdir();(self.cache/'one').write_bytes(b'preserve\x00raw');(self.cache/'sub/two').write_bytes(b'old-cache');self.facts=dict(source_root=str(self.cache),authorized=True,exclusive=True,rebuildable=True,inactive=True,dependencies_clear=True,locks_owned=True);self.calls=0
  def guard():self.calls+=1;return dict(self.facts)
  files=[]
  for p in sorted(x for x in self.cache.rglob('*') if x.is_file()):
   s=p.stat();files.append(dict(path=p.relative_to(self.cache).as_posix(),bytes=s.st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),inode=s.st_ino,device=s.st_dev,mtime_ns=s.st_mtime_ns,mode=stat.S_IMODE(s.st_mode),uid=s.st_uid,links=s.st_nlink))
  self.manifest=dict(source_root=str(self.cache),approved_exact_root=str(self.cache),cache_kind='exclusive-rebuildable',files=files,directories=['sub'],_guard=guard);self.tar=self.base/'archive.tar'
  with tarfile.open(str(self.tar),'w') as t:
   for r in files:t.add(str(self.cache/r['path']),arcname=r['path'],recursive=False)
 def tearDown(self):
  # Only the independently-created and absolute-path-checked test directory.
  self.assertEqual(os.path.commonpath([str(self.base),str(pathlib.Path('build/u21-test-temporary').resolve())]),str(pathlib.Path('build/u21-test-temporary').resolve()));self.temp.cleanup()
 def verified(self):
  r=archive.archive_verified(self.manifest,self.tar);self.assertTrue(r.get('verified'),'U21 every-member archive proof is required');return r
 def test_complete_archive_before_exact_release(self):
  receipt=self.verified();self.assertEqual(receipt['member_count'],2);result=archive.release_verified(self.manifest,receipt);self.assertTrue(result['released']);self.assertTrue(self.cache.is_dir());self.assertEqual(list(self.cache.iterdir()),[]);self.assertTrue(self.tar.is_file());self.assertGreaterEqual(self.calls,3)
 def test_new_write_prevents_any_unlink(self):
  receipt=self.verified();(self.cache/'new').write_bytes(b'new writer')
  with self.assertRaises(ValueError):archive.release_verified(self.manifest,receipt)
  self.assertTrue((self.cache/'one').is_file());self.assertTrue((self.cache/'sub/two').is_file())
 def test_archive_member_mismatch_prevents_release(self):
  receipt=self.verified();other=self.base/'other';other.write_bytes(b'bad');
  with tarfile.open(str(self.tar),'w') as t:t.add(str(other),arcname='one',recursive=False)
  with self.assertRaises(ValueError):archive.release_verified(self.manifest,receipt)
  self.assertTrue((self.cache/'one').is_file())
 def test_changed_file_same_size_prevents_release(self):
  receipt=self.verified();(self.cache/'sub/two').write_bytes(b'new-cache')
  with self.assertRaises(ValueError):archive.release_verified(self.manifest,receipt)
  self.assertTrue((self.cache/'one').is_file())
 def test_fresh_activity_or_dependency_refusal(self):
  receipt=self.verified()
  for field in ('authorized','exclusive','inactive','dependencies_clear','locks_owned'):
   self.facts[field]=False
   with self.assertRaises(ValueError):archive.release_verified(self.manifest,receipt)
   self.facts[field]=True
  self.assertTrue((self.cache/'one').is_file())
 def test_shared_hardlink_and_testdisk_never_selected(self):
  self.manifest['cache_kind']='shared-cache'
  with self.assertRaises(ValueError):archive.archive_verified(self.manifest,self.tar)
  self.manifest['cache_kind']='exclusive-rebuildable';os.link(str(self.cache/'one'),str(self.base/'outside-link'))
  with self.assertRaises(ValueError):archive.release_verified(self.manifest,{'verified':True})
  self.manifest['source_root']=str(self.base/'testdisk'/'sstate-cache')
  with self.assertRaises(ValueError):archive.archive_verified(self.manifest,self.tar)
 def test_real_existing_C_locks_are_not_reentrant(self):
  from scripts.demo.coordination import coordinated
  with self.assertRaises((BlockingIOError,ValueError)):
   with coordinated('/home/z2euss/workspace/linux_nn_runtime_upgrade_20261021/env.json',pathlib.Path.cwd(),'unit'):self.fail('existing three ownership locks were bypassed')
if __name__=='__main__':unittest.main()
