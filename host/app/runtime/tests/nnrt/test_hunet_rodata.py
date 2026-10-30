"""Counterexamples on real frozen/derived RV32 expf objects, not a GPU test."""
import pathlib,sys,unittest
from scripts.unified.hunet_rodata import frozen,check_derived,section_rows
class ReadonlyPlacement(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  root=pathlib.Path(sys.argv[-1]);cls.original=(root/'expf-original.o').read_bytes();cls.derived=(root/'expf-readonly.o').read_bytes()
 def test_actual_function_and_symbols(self):self.assertTrue(check_derived(self.original,self.derived)['source_instructions_and_relocations_identical'])
 def test_unknown_original(self):
  b=bytearray(self.original);b[-1]^=1
  with self.assertRaisesRegex(ValueError,'EXPF_FROZEN_OBJECT_IDENTITY'):frozen(bytes(b))
 def test_constant_code_relocations_and_symbols_fail_closed(self):
  for name,offset in [('.rodata.vxnn.expf.constants',0),('.text.expf',0),('.rela.text.expf',0),('.symtab',19*16+4)]:
   b=bytearray(self.derived);row=section_rows(b)[name]['row'];b[row[4]+offset]^=4
   with self.subTest(section=name):
    with self.assertRaises(ValueError):check_derived(self.original,bytes(b))
if __name__=='__main__':
 target=sys.argv.pop();suite=unittest.defaultTestLoader.loadTestsFromTestCase(ReadonlyPlacement);sys.argv.append(target);result=unittest.TextTestRunner(verbosity=2).run(suite);raise SystemExit(not result.wasSuccessful())
