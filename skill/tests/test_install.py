import importlib.util,os,pathlib,tempfile,unittest
P=pathlib.Path
S=importlib.util.spec_from_file_location('installer',P(__file__).parents[1]/'install_skills.py');M=importlib.util.module_from_spec(S);S.loader.exec_module(M)
class InstallTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(dir=os.environ.get('SKILL_TEST_TMP'));self.addCleanup(self.tmp.cleanup);self.root=P(self.tmp.name)
  self.source=self.root/'source';self.source.mkdir();self.dest=self.root/'target'
  for name in ['first-skill','second-skill']:
   p=self.source/name;p.mkdir();(p/'SKILL.md').write_text('---\nname: '+name+'\ndescription: Use when testing.\n---\n',encoding='utf8')
 def test_install_selected_portable_directory(self):
  result=M.install(self.source,self.dest,['second-skill']);self.assertEqual(result,['second-skill']);self.assertTrue((self.dest/'second-skill/SKILL.md').exists());self.assertFalse((self.dest/'first-skill').exists())
 def test_collision_refuses_before_partial_install(self):
  (self.dest/'second-skill').mkdir(parents=True);(self.dest/'second-skill/keep').write_text('keep')
  with self.assertRaises(ValueError):M.install(self.source,self.dest,None)
  self.assertFalse((self.dest/'first-skill').exists());self.assertEqual((self.dest/'second-skill/keep').read_text(),'keep')
 def test_nested_destination_and_unknown_skill_refused(self):
  for dest,names in [(self.source/'nested',None),(self.dest,['../first-skill']),(self.dest,['unknown'])]:
   with self.subTest(dest=dest,names=names):
    with self.assertRaises(ValueError):M.install(self.source,dest,names)
if __name__=='__main__':unittest.main()
