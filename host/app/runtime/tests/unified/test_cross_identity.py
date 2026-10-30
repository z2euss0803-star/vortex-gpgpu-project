import pathlib,tempfile,unittest,json,os,subprocess
from scripts.unified.cross_common import snapshot,sha,verify_source_lock,new_build_output
from scripts.unified.build_userspace import TARGETS
class CrossIdentity(unittest.TestCase):
 def test_every_cross_elf_target_has_a_real_existing_make_rule(self):
  root=pathlib.Path(__file__).resolve().parents[2];text=(root/'Makefile.unified').read_text();import re
  self.assertEqual(len(TARGETS),14);self.assertEqual(len(set(TARGETS)),14)
  for name in TARGETS:
   with self.subTest(target=name):self.assertRegex(text,r'(?m)^\$\(OUT\)/'+re.escape(name)+r'\s*:')
 def test_image_p8_and_legacy_adapter_changes_have_runtime_identity(self):
  with tempfile.TemporaryDirectory() as directory:
   root=pathlib.Path(directory)
   for name in ('image/platform_unified.cpp','p8/renderer.cpp','adapters/linux.cpp','vendor/image-studio/pipeline.c'):
    p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'int original;\n');before=snapshot(root);p.write_bytes(b'int changed;\n');after=snapshot(root)
    with self.subTest(path=name):self.assertNotEqual(before['runtime_source_id'],after['runtime_source_id'])
 def test_regular_build_output_is_new_and_scoped(self):
  base=(pathlib.Path.cwd()/'build/U22-identity-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   directory=pathlib.Path(name).resolve();self.assertEqual(directory.parent,base);root=directory/'source';root.mkdir();output=root/'build'/'new-target';self.assertEqual(new_build_output(root,output),output);self.assertTrue(output.is_dir())
 def test_build_directory_link_cannot_redirect_cross_outputs_outside_source(self):
  base=(pathlib.Path.cwd()/'build/U22-identity-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   directory=pathlib.Path(name).resolve();self.assertEqual(directory.parent,base);root=directory/'source';root.mkdir();outside=directory/'outside';outside.mkdir();link=root/'build'
   if os.name=='nt':subprocess.run(['cmd.exe','/d','/c','mklink','/J',str(link),str(outside)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
   else:link.symlink_to(outside,target_is_directory=True)
   try:
    self.assertEqual(link.resolve(),outside)
    with self.assertRaises(ValueError):new_build_output(root,link/'unapproved-output')
    self.assertFalse((outside/'unapproved-output').exists())
   finally:
    if os.name=='nt':os.rmdir(str(link))
    else:link.unlink()
 def test_package_only_extension_changes_snapshot_but_not_runtime_source(self):
  base=(pathlib.Path.cwd()/'build/U22-identity-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   root=pathlib.Path(name).resolve();self.assertEqual(root.parent,base);(root/'nnrt').mkdir();(root/'nnrt/op.cpp').write_bytes(b'int existing_operator;\n');packages=root/'data/nnrt/packages';packages.mkdir(parents=True);(packages/'a.json').write_bytes(b'{"model":"A"}');a=snapshot(root)
   (packages/'b.json').write_bytes(b'{"model":"B"}');b=snapshot(root);self.assertNotEqual(a['source_snapshot_id'],b['source_snapshot_id']);self.assertEqual(a['runtime_source_id'],b['runtime_source_id'])
   (root/'nnrt/op.cpp').write_bytes(b'int different_operator;\n');c=snapshot(root);self.assertNotEqual(b['runtime_source_id'],c['runtime_source_id']);self.assertNotEqual(c['source_snapshot_id'],c['runtime_source_id'])
 def test_changed_source_cannot_compile_using_an_old_frozen_lock(self):
  base=(pathlib.Path.cwd()/'build/U22-identity-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   directory=pathlib.Path(name).resolve();self.assertEqual(directory.parent,base);root=directory/'source';(root/'nnrt').mkdir(parents=True);(root/'nnrt/op.cpp').write_bytes(b'int frozen;\n');before=snapshot(root);lock=directory/'source-lock.json';lock.write_text(json.dumps(dict(schema=1,snapshot=before)));digest=sha(lock);self.assertEqual(verify_source_lock(root,lock,digest),before)
   (root/'nnrt/op.cpp').write_bytes(b'int unapproved_change;\n')
   with self.assertRaises(ValueError):verify_source_lock(root,lock,digest)
 def test_source_lock_boolean_is_not_an_integer_byte_count(self):
  base=(pathlib.Path.cwd()/'build/U22-identity-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   directory=pathlib.Path(name).resolve();self.assertEqual(directory.parent,base);root=directory/'source';(root/'nnrt').mkdir(parents=True);(root/'nnrt/one.c').write_bytes(b'x');state=snapshot(root);self.assertEqual(state['files'][0]['bytes'],1);state['files'][0]['bytes']=True;state['runtime_files'][0]['bytes']=True;lock=directory/'typed-lock.json';lock.write_text(json.dumps(dict(schema=1,snapshot=state)))
   with self.assertRaises(ValueError):verify_source_lock(root,lock,sha(lock))
if __name__=='__main__':unittest.main()
