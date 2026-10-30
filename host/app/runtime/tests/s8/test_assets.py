import struct,unittest
from scripts.s8.assets import payloads,objects
from scripts.s8.catalog import expected_cases
from scripts.s8.layout import guard_byte
class Assets(unittest.TestCase):
 def test_catalog_payloads_and_full_stack_object(self):
  for case in expected_cases():
   a,b=payloads(case,15);self.assertEqual((len(a),len(b)),(case['a_bytes'],case['b_bytes']))
  case=next(r for r in expected_cases() if r['id']==5);rows=objects(case,b'CONTROLLED_TEST_BYTES')
  self.assertEqual(len(rows),53);self.assertEqual(rows[0]['data'][:21],b'CONTROLLED_TEST_BYTES')
  self.assertEqual(rows[18]['data'][64:96],struct.pack('<8I',0,0,65536,0,0,65536,65536,65536))
  self.assertEqual(rows[52]['data'][:64],bytes(guard_byte(8,63,0,i) for i in range(64)))
  self.assertEqual(rows[52]['data'][-64:],bytes(guard_byte(8,0,1,i) for i in range(64)))
if __name__=='__main__':unittest.main()
