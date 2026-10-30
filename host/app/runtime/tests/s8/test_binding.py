import importlib,struct,unittest
class Binding(unittest.TestCase):
 def test_14_digest_order_no_self_cycle_and_missing_hardware(self):
  api=importlib.import_module('scripts.s8.binding');values={n:('%02x'%(i+1))*32 for i,n in enumerate(api.ORDER)}
  raw=api.make_binding(values);self.assertEqual(len(raw),512)
  self.assertEqual(struct.unpack('<8I',raw[:32]),(0x31423853,1,512,5,11,1,1,0));self.assertEqual(raw[-32:],bytes(32))
  for i,n in enumerate(api.ORDER):self.assertEqual(raw[32+i*32:64+i*32],bytes.fromhex(values[n]))
  for name in api.ORDER:
   wrong=dict(values);wrong[name]='0'*64
   with self.assertRaises(ValueError):api.make_binding(wrong)
  with self.assertRaises(ValueError):api.make_binding(dict(values,binding='a'*64))
if __name__=='__main__':unittest.main()
