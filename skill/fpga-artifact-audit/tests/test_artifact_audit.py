import importlib.util,json,pathlib,tempfile,unittest,hashlib,os
P=pathlib.Path
SPEC=importlib.util.spec_from_file_location('artifact_audit',P(__file__).parents[1]/'scripts/audit_artifacts.py')
M=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(M)

class ArtifactTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(dir=os.environ.get('SKILL_TEST_TMP'));self.addCleanup(self.temp.cleanup);self.root=P(self.temp.name)
  (self.root/'rtl.bin').write_bytes(b'line1\r\nline2\n')
  self.rows=[{'path':'rtl.bin','bytes':13,'sha256':hashlib.sha256(b'line1\r\nline2\n').hexdigest()}]
 def test_exact_bytes(self):
  self.assertEqual(M.audit(self.root,{'files':self.rows})['status'],'VERIFIED')
 def test_crlf_change_is_failure(self):
  (self.root/'rtl.bin').write_bytes(b'line1\nline2\n');self.assertEqual(M.audit(self.root,{'files':self.rows})['status'],'MISMATCH')
 def test_escape_and_absolute_rejected(self):
  for p in ['../secret','/tmp/secret','C:/secret','..\\secret']:
   with self.subTest(p=p):
    r=dict(self.rows[0],path=p)
    self.assertEqual(M.audit(self.root,{'files':[r]})['status'],'INVALID_MANIFEST')
 def test_duplicate_and_empty_rejected(self):
  self.assertEqual(M.audit(self.root,{'files':self.rows*2})['status'],'INVALID_MANIFEST')
  self.assertEqual(M.audit(self.root,{'files':[]})['status'],'INVALID_MANIFEST')
 def test_missing_is_explicit(self):
  (self.root/'rtl.bin').unlink();r=M.audit(self.root,{'files':self.rows});self.assertEqual(r['status'],'MISMATCH');self.assertEqual(r['issues'][0]['reason'],'MISSING')
 def test_bool_size_rejected(self):
  r=dict(self.rows[0],bytes=True);self.assertEqual(M.audit(self.root,{'files':[r]})['status'],'INVALID_MANIFEST')
 def test_trailing_dot_or_space_rejected(self):
  for name in ['rtl.bin.','rtl.bin ','folder. /rtl.bin']:
   with self.subTest(name=name):self.assertEqual(M.audit(self.root,{'files':[dict(self.rows[0],path=name)]})['status'],'INVALID_MANIFEST')
 @unittest.skipUnless(os.name=='nt','case alias behavior depends on host filesystem')
 def test_windows_case_alias_duplicate_rejected(self):
  rows=self.rows+[dict(self.rows[0],path='RTL.BIN')]
  self.assertEqual(M.audit(self.root,{'files':rows})['status'],'INVALID_MANIFEST')
 def test_symlink_not_followed(self):
  link=self.root/'ref'
  try:link.symlink_to(self.root/'rtl.bin')
  except OSError:self.skipTest('symlink creation not permitted on this host')
  r=dict(self.rows[0],path='ref');self.assertEqual(M.audit(self.root,{'files':[r]})['status'],'MISMATCH')

if __name__=='__main__':unittest.main()
