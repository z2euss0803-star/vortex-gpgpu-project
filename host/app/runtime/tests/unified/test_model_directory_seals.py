"""Real recipe archive metadata regression for FIELD-1836; no GPU claims."""
import ast, contextlib, copy, hashlib, importlib.util, json, os, pathlib, stat, subprocess, sys, tarfile, tempfile, unittest
from scripts.unified.audit_image import normalize_cpio_entries, payload_errors

ROOT=pathlib.Path(__file__).resolve().parents[2]


@contextlib.contextmanager
def recipe_workspace():
 # Python 3.6 TemporaryDirectory cannot repair read-only fixture directories.
 # Restore traversal/write permissions even when an assertion fails, after all
 # assertions have observed the actual extracted and installed modes.
 with tempfile.TemporaryDirectory() as temp:
  try:yield temp
  finally:
   for directory,children,files in os.walk(temp,topdown=True,followlinks=False):
    os.chmod(directory,0o700)

def fixture():
 spec=importlib.util.spec_from_file_location('payload_fixture',str(ROOT/'tests/unified/test_image_payload.py'))
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 entries,manifest=module.InstalledPayload().fixture()
 raw=b'{"version":2,"nn":[{"package":"/usr/share/vxgpu/models/hunet"},{"package":"/usr/share/vxgpu/models/mosaic"}]}\n'
 entries['etc/vxgpud/installation.json']['data']=raw
 manifest['files']['etc/vxgpud/installation.json'].update(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
 for name in ('usr','usr/share','usr/share/vxgpu','usr/share/vxgpu/models'):
  entries[name]=dict(kind='directory',mode=0o755,uid=0,gid=0)
 for name in ('hunet','mosaic'):
  directory='usr/share/vxgpu/models/'+name
  entries[directory]=dict(kind='directory',mode=0o500,uid=0,gid=0)
  raw=b'EXPLICIT_UNIT_MODEL_NOT_INFERENCE\n';path=directory+'/manifest.json'
  entries[path]=dict(kind='file',mode=0o400,uid=0,gid=0,data=raw)
  manifest['files'][path]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),mode=0o400)
 return entries,manifest

class ModelDirectorySeals(unittest.TestCase):
 def test_same_model_file_hashes_do_not_admit_writable_missing_or_linked_root(self):
  for change in ('writable','missing','symlink','regular_file','wrong_owner','wrong_group','not_searchable','not_readable','special_mode'):
   entries,manifest=fixture();name='usr/share/vxgpu/models/hunet'
   if change=='writable':entries[name]['mode']=0o755
   elif change=='missing':del entries[name]
   elif change=='symlink':entries[name].update(kind='symlink',link='/tmp/model')
   elif change=='regular_file':entries[name]['kind']='file'
   elif change=='wrong_owner':entries[name]['uid']=1000
   elif change=='wrong_group':entries[name]['gid']=1000
   elif change=='not_searchable':entries[name]['mode']=0o400
   elif change=='not_readable':entries[name]['mode']=0o100
   else:entries[name]['mode']=0o4500
   with self.subTest(change=change):
    self.assertTrue(any('model directory' in e for e in payload_errors(entries,manifest)),
                    'model root metadata is required even when every file hash matches')

 def test_readonly_root_and_nested_directories_are_accepted(self):
  entries,manifest=fixture()
  entries['usr/share/vxgpu/models/mosaic/nested']=dict(kind='directory',mode=0o500,uid=0,gid=0)
  self.assertEqual(payload_errors(entries,manifest),[])
  entries['usr/share/vxgpu/models/mosaic/nested']['mode']=0o755
  self.assertTrue(any('model directory' in e for e in payload_errors(entries,manifest)))

 def test_missing_or_hostile_nested_directory_is_rejected_with_unchanged_file_seals(self):
  for change in ('missing','symlink','writable','wrong_owner','not_searchable'):
   entries,manifest=fixture();name='usr/share/vxgpu/models/mosaic/a/b'
   for parent in ('usr/share/vxgpu/models/mosaic/a',name):entries[parent]=dict(kind='directory',mode=0o500,uid=0,gid=0)
   source=entries['usr/share/vxgpu/models/mosaic/manifest.json'];entries[name+'/weights.bin']=dict(source)
   manifest['files'][name+'/weights.bin']=dict(manifest['files']['usr/share/vxgpu/models/mosaic/manifest.json'])
   self.assertEqual(payload_errors(entries,manifest),[])
   if change=='missing':del entries[name]
   elif change=='symlink':entries[name].update(kind='symlink',link='/tmp/weights')
   elif change=='writable':entries[name]['mode']=0o700
   elif change=='wrong_owner':entries[name]['uid']=1000
   else:entries[name]['mode']=0o400
   with self.subTest(change=change):self.assertTrue(any('model directory' in e for e in payload_errors(entries,manifest)))

 def test_model_ancestors_must_be_real_root_owned_and_searchable(self):
  for change in ('missing','symlink','writable','wrong_owner','not_searchable'):
   entries,manifest=fixture();name='usr/share/vxgpu/models'
   if change=='missing':del entries[name]
   elif change=='symlink':entries[name].update(kind='symlink',link='/tmp/models')
   elif change=='writable':entries[name]['mode']=0o777
   elif change=='wrong_owner':entries[name]['uid']=1000
   else:entries[name]['mode']=0o644
   with self.subTest(change=change):self.assertTrue(any('model directory' in e for e in payload_errors(entries,manifest)))

 def test_declared_empty_missing_package_is_not_hidden_by_file_inventory(self):
  entries,manifest=fixture();prefix='usr/share/vxgpu/models/hunet'
  entries={n:r for n,r in entries.items() if not n.startswith(prefix)}
  manifest['files']={n:r for n,r in manifest['files'].items() if not n.startswith(prefix)}
  self.assertTrue(any('model directory' in e for e in payload_errors(entries,manifest)))

 def test_real_cpio_parser_preserves_directory_kind_and_strictness(self):
  old_path=list(sys.path)
  try:
   sys.path.insert(0,str(ROOT/'scripts/cnn'))
   spec=importlib.util.spec_from_file_location('directory_seal_cpio',ROOT/'scripts/cnn/image_audit.py')
   parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
  finally:sys.path[:]=old_path
  def member(name,row,inode):
   kind={'file':stat.S_IFREG,'directory':stat.S_IFDIR,'symlink':stat.S_IFLNK}[row['kind']]
   data=row['link'].encode() if row['kind']=='symlink' else row.get('data',b'');encoded=name.encode()+b'\0'
   fields=[inode,kind|row['mode'],row['uid'],row['gid'],1,0,len(data),0,0,0,0,len(encoded),0]
   header=b'070701'+b''.join(('%08x'%v).encode() for v in fields)+encoded
   return header+b'\0'*(-len(header)%4)+data+b'\0'*(-len(data)%4)
  entries,manifest=fixture()
  raw=b''.join(member(n,r,i) for i,(n,r) in enumerate(entries.items(),1))
  trailer=member('TRAILER!!!',dict(kind='file',mode=0,uid=0,gid=0),999)
  actual=normalize_cpio_entries(parser.cpio_entries(raw+trailer))
  self.assertEqual(actual['usr/share/vxgpu/models/hunet']['kind'],'directory')
  self.assertEqual(payload_errors(actual,manifest),[])
  actual['usr/share/vxgpu/models/hunet']['mode']=0o755
  self.assertTrue(any('model directory' in e for e in payload_errors(actual,manifest)))
  with self.assertRaisesRegex(ValueError,'Missing cpio trailer'):parser.cpio_entries(raw)
  with self.assertRaisesRegex(ValueError,'Duplicate cpio path'):parser.cpio_entries(raw+member('usr/share/vxgpu/models/hunet',entries['usr/share/vxgpu/models/hunet'],998)+trailer)

 def test_directory_policy_change_cannot_relabel_a_frozen_producer(self):
  from scripts.unified.audit_image import image_source_provenance
  from scripts.unified.cross_common import snapshot,sha
  from tests.unified.test_image_runtime_closure import ProducerAuditorIdentity
  old,new,built,payload,lock,digest=ProducerAuditorIdentity.fixture(self)
  for root in (old,new):
   path=root/'scripts/unified/audit_image.py'
   path.write_text(path.read_text()+'def model_directory_errors(): return []\n')
  before=snapshot(old)
  for receipt in (built,payload):receipt.update(source_before=before,source_after=before)
  lock.write_text(json.dumps(dict(schema=1,snapshot=before)));digest=sha(lock)
  self.assertEqual(image_source_provenance(new,built,payload,old,lock,digest)['changed_checker_paths'],[])
  path=new/'scripts/unified/audit_image.py'
  path.write_text(path.read_text().replace('def model_directory_errors(): return []','def model_directory_errors(): return ["different policy"]'))
  with self.assertRaisesRegex(ValueError,'admission policy changed'):image_source_provenance(new,built,payload,old,lock,digest)

 @unittest.skipUnless(os.name=='posix','native POSIX chmod and GNU tar/cp extraction required')
 def test_actual_recipe_archives_include_searchable_readonly_package_roots(self):
  # Execute the actual production recipe-packaging loop, stopping before any
  # PetaLinux command. This does not duplicate that loop in the test.
  from scripts.unified.cross_common import sha
  source=ROOT/'scripts/unified/image.py';tree=ast.parse(source.read_text())
  function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='prepare_image')
  node=next(n for n in ast.walk(function) if isinstance(n,ast.For)
            and isinstance(n.target,ast.Tuple)
            and [v.id for v in n.target.elts if isinstance(v,ast.Name)]==['package','template'])
  program=ast.parse('pass');program.body=[copy.deepcopy(node)];ast.fix_missing_locations(program)
  with recipe_workspace() as temp:
   base=pathlib.Path(temp);stage=base/'stage';meta=base/'meta';stage.mkdir()
   entries,manifest=fixture()
   nested='usr/share/vxgpu/models/mosaic/nested/deep'
   for name in ('usr/share/vxgpu/models/mosaic/nested',nested):entries[name]=dict(kind='directory',mode=0o500,uid=0,gid=0)
   entries[nested+'/weights.bin']=dict(entries['usr/share/vxgpu/models/mosaic/manifest.json'])
   manifest['files'][nested+'/weights.bin']=dict(manifest['files']['usr/share/vxgpu/models/mosaic/manifest.json'])
   for name,row in entries.items():
    p=stage/name
    if row['kind']=='directory':p.mkdir(parents=True,exist_ok=True)
    elif row['kind']=='file':
     p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(row['data']);p.chmod(row['mode'])
   for name,row in entries.items():
    if row['kind']=='directory':(stage/name).chmod(row['mode'])
   (stage/'usr/share/vxgpu/payload-manifest.json').write_text(json.dumps(manifest))
   def write(name,raw):
    p=meta/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw);return p
   payload=dict(payload=manifest)
   owned={'etc/codex-build-id','etc/codex-provider-manifest.json','etc/init.d/codex-direct-network','etc/network/interfaces'}
   namespace=dict(root=ROOT,meta=meta,stage=stage,payload=payload,owned=owned,write=write,
                  pathlib=pathlib,tarfile=tarfile,stat=stat,sha=sha)
   exec(compile(program,str(source),'exec'),namespace)
   with tarfile.open(str(meta/'recipes-apps/unified-assets/files/unified-assets.tar.gz')) as archive:
    members={m.name:m for m in archive.getmembers()}
    for name in ('usr/share/vxgpu/models/hunet','usr/share/vxgpu/models/mosaic','usr/share/vxgpu/models/mosaic/nested',nested):
     self.assertIn(name,members,'recipe must not lose directory seal during file-only packing')
     self.assertTrue(members[name].isdir());self.assertEqual(members[name].mode,0o500)
     self.assertEqual((members[name].uid,members[name].gid),(0,0))
   unpacked=base/'unpacked';installed=base/'installed';unpacked.mkdir();installed.mkdir()
   result=subprocess.run(['tar','--no-same-owner','-xzf',str(meta/'recipes-apps/unified-assets/files/unified-assets.tar.gz'),'-C',str(unpacked)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True)
   self.assertEqual(result.returncode,0,result.stderr)
   result=subprocess.run(['cp','-a',str(unpacked)+'/.',str(installed)+'/'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True)
   self.assertEqual(result.returncode,0,result.stderr)
   for directory in (unpacked,installed):
    for name in ('usr/share/vxgpu/models/hunet','usr/share/vxgpu/models/mosaic','usr/share/vxgpu/models/mosaic/nested',nested):
     self.assertEqual(stat.S_IMODE((directory/name).stat().st_mode),0o500)
    for name,row in manifest['files'].items():
     if name.startswith('usr/share/vxgpu/models/'):
      self.assertEqual(hashlib.sha256((directory/name).read_bytes()).hexdigest(),row['sha256'])

if __name__=='__main__':unittest.main()
