"""Execute actual build admission expressions against distinct volume facts."""
import ast,copy,pathlib,tempfile,unittest
from scripts.unified import resources as admission

ROOT=pathlib.Path(__file__).resolve().parents[2]
G=1024**3


class BuildResourceVolume(unittest.TestCase):
 def facts(self,disk):
  return dict(host='z2euss-virtual-machine',account='z2euss',source_identity_verified=True,locks_owned=True,other_heavy=[],available_ram=16*G,free_disk=disk,worker_count=1)

 def gate(self,script,function,namespace):
  tree=ast.parse((ROOT/script).read_text())
  body=next(n.body for n in tree.body if isinstance(n,ast.FunctionDef) and n.name==function)
  statement=next(n for n in body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='resources' for t in n.targets))
  program=ast.parse('pass');program.body=[copy.deepcopy(statement)];ast.fix_missing_locations(program)
  namespace.update(resource_gate=admission.resource_gate,build_output_resource_path=getattr(admission,'build_output_resource_path',None))
  exec(compile(program,str(ROOT/script),'exec'),namespace)
  return namespace['resources']

 def test_new_kernel_admits_actual_output_volume_even_if_parent_source_disk_is_low(self):
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp).resolve();root=base/'candidate';old=base/'immutable-parent';target=root/'build';target.mkdir(parents=True);old.mkdir();out=target/'new-kernel';calls=[]
   def facts(path):
    path=pathlib.Path(path).resolve();calls.append(path);return self.facts(20*G if path==target else G)
   try:result=self.gate('scripts/unified/bind_kernel.py','build_kernel',dict(root=root,old=old,out=out,resource_facts=facts))
   except ValueError as error:self.fail('enough space on actual candidate output volume was rejected: '+str(error))
   self.assertEqual(calls,[target]);self.assertEqual(result['required_disk'],15*G);self.assertEqual(result['worker_count'],1);self.assertFalse(out.exists())

 def test_new_kernel_refuses_low_actual_output_volume_even_if_parent_source_disk_is_large(self):
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp).resolve();root=base/'candidate';old=base/'immutable-parent';target=root/'build';target.mkdir(parents=True);old.mkdir();out=target/'new-kernel';calls=[]
   def facts(path):
    path=pathlib.Path(path).resolve();calls.append(path);return self.facts(15*G-1 if path==target else 40*G)
   with self.assertRaisesRegex(ValueError,'insufficient fresh RAM/disk'):
    self.gate('scripts/unified/bind_kernel.py','build_kernel',dict(root=root,old=old,out=out,resource_facts=facts))
   self.assertEqual(calls,[target]);self.assertFalse(out.exists())

 def test_complete_image_gate_stays_on_actual_existing_project_volume(self):
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp).resolve();project=base/'existing-project';project.mkdir();calls=[]
   def facts(path):calls.append(pathlib.Path(path).resolve());return self.facts(15*G)
   result=self.gate('scripts/unified/image.py','prepare_image',dict(project=project,resource_facts=facts))
   self.assertEqual(calls,[project]);self.assertEqual(result['required_disk'],15*G);self.assertEqual(result['worker_count'],1)

 def test_output_probe_does_not_create_missing_paths_and_preserves_boundary(self):
  with tempfile.TemporaryDirectory() as temp:
   root=pathlib.Path(temp).resolve()/'candidate';root.mkdir();out=root/'build/missing/nested-run'
   self.assertEqual(admission.build_output_resource_path(root,out),root)
   self.assertFalse((root/'build').exists())
   for invalid in (root/'outside',root/'build',root/'build/../../outside'):
    with self.subTest(path=str(invalid)),self.assertRaises(ValueError):admission.build_output_resource_path(root,invalid)


if __name__=='__main__':unittest.main()
