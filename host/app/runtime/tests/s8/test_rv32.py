import copy,importlib,tempfile,unittest
from pathlib import Path
from scripts.s8.crt import audit_facts
class RV32(unittest.TestCase):
 def test_elf_flat_contract_rejects_partial_and_bad_tls(self):
  api=importlib.import_module('scripts.s8.audit_rv32')
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'partial.o').write_bytes(b'\x7fELF\x01')
   with self.assertRaises(ValueError):
    api.audit(p/'partial.o',p/'flat',p/'map',p/'disasm',p/'layout')
  with self.assertRaises(ValueError):api.stack_bound({'a':16},{'a':['a']},['a'])
  with self.assertRaises(ValueError):api.stack_bound({'a':16},{'a':['unknown']},['a'])
  with self.assertRaises(ValueError):api.stack_bound({'a':8128},{},['a'])
  self.assertEqual(api.stack_bound({'a':16,'b':32},{'a':['b']},['a']),48)
 def test_actual_instruction_masks_and_no_missing_operations(self):
  api=importlib.import_module('scripts.s8.audit_rv32')
  words=[0x0406840b,0x0407040b,0x0407840b,0x0400800b,0x0000002b,0x0ff0000f,0x0000000b]
  self.assertEqual(api.audit_words(words)['s8_formats'],[13,14,15])
  for i in range(5):
   with self.assertRaises(ValueError):api.audit_words(words[:i]+words[i+1:])
 def test_static_initializer_callbacks_require_actual_table_bytes(self):
  api=importlib.import_module('scripts.s8.audit_rv32')
  self.assertEqual(api.array_callbacks(b'',{},{}),dict(init=[],fini=[]))
  raw=(0x80000100).to_bytes(4,'little')
  section=(0,1,0,0,0,4,0,0,4,0)
  self.assertEqual(api.array_callbacks(raw,{'.fini_array':section},{'cleanup':dict(value=0x80000100)})['fini'],['cleanup'])
  with self.assertRaises(ValueError):api.array_callbacks(raw,{'.fini_array':section},{})
 def test_crt_stack_pivot_is_exact_and_compiled_frames_are_not_redecoded(self):
  api=importlib.import_module('scripts.s8.audit_rv32')
  words=[0xffff0137,0xf14022f3,0x00d29313,0x40610133]
  self.assertEqual(api.machine_frame('init_regs',words,{}),0)
  with self.assertRaises(ValueError):api.machine_frame('init_regs',words[:-1],{})
  self.assertEqual(api.machine_frame('compiled',[0x40610133],{'compiled':32}),32)
if __name__=='__main__':unittest.main()
