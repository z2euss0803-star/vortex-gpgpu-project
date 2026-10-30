import unittest,struct,hashlib,pathlib,runpy,tempfile,sys
from scripts.unified.build_catalog import build_catalog
class CatalogTests(unittest.TestCase):
 def fixture(self):
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)/'catalog';saved=sys.argv;sys.argv=['fixture',str(p)]
   try:runpy.run_path('tests/v2/catalog_fixture.py',run_name='__main__')
   finally:sys.argv=saved
   return p.read_bytes()
 def entry(self):
  raw=self.fixture();return {'fields':list(struct.unpack('<16I',raw[64:128])),'digests':[raw[128+32*i:160+32*i] for i in range(8)],'schema':raw[576:]}
 def test_exact_independent_LE_golden(self):
  self.assertEqual(build_catalog([self.entry()]),self.fixture())
 def test_capacity_and_invalid_metadata(self):
  e=self.entry()
  for bad in ([],[e]*17):
   with self.assertRaises(ValueError):build_catalog(bad)
  for index,value in ((0,-1),(15,1),(5,8388609)):
   e=self.entry();e['fields'][index]=value
   with self.assertRaises(ValueError):build_catalog([e])
 def test_digest_does_not_trust_provided_schema_or_entry_id(self):
  e=self.entry();expected=self.fixture();e['digests'][2]=bytes(32);e['digests'][6]=bytes([8])*32
  self.assertEqual(build_catalog([e]),expected)
if __name__=='__main__':unittest.main()
