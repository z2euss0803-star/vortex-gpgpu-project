import hashlib,pathlib,struct,unittest
from scripts.unified.cross_common import provider
from scripts.unified.bind_kernel import firmware_inputs,catalog_entries

ROOT=pathlib.Path(__file__).resolve().parents[2]
class S8JointPlatformTests(unittest.TestCase):
 def joint(self):
  try:return firmware_inputs(ROOT,4)
  except (ValueError,StopIteration,KeyError) as error:self.fail('versioned S8 joint production platform is missing: '+str(error))
 def test_versioned_joint_keeps_exact_s8_hardware(self):
  joint=self.joint()['provider'];old=provider(ROOT,1)
  self.assertEqual(joint['compatible'],'vortex,hw4-s8-joint-v1')
  self.assertEqual(joint['namespaces'],[1,2,3])
  for key in ('hardware_contract','bit_sha256','xsa_sha256'):self.assertEqual(joint[key],old[key])
  self.assertFalse(joint['board_identity_verified'])
 def test_sixteen_original_programs_and_bounded_catalog(self):
  joint=self.joint();rows=catalog_entries(joint['catalog'])
  self.assertEqual(len(rows),16);self.assertLessEqual(len(joint['catalog']),65536)
  self.assertEqual([(r['fields'][0],r['fields'][2]) for r in rows],[(1,65552),(1,65568)]+[(3,512+i) for i in range(10)]+[(2,i) for i in (9,10,11,12)])
  self.assertNotIn(65537,[r['fields'][2] for r in rows])
 def test_shader_rodata_schema_bytes_preserved_and_execution_rebound(self):
  joint=self.joint();new=catalog_entries(joint['catalog']);original={}
  for flag in (1,2):
   old=firmware_inputs(ROOT,flag)
   for row,item in zip(catalog_entries(old['catalog']),old['entries']):original[(row['fields'][0],row['fields'][2])]=(row,item)
  self.assertEqual(struct.unpack_from('<4I',joint['execution']),(0x454c5856,2,64+256*16,16))
  self.assertEqual(joint['execution'][16:48],hashlib.sha256(joint['catalog']).digest())
  for i,(row,item) in enumerate(zip(new,joint['entries'])):
   old,payload=original[(row['fields'][0],row['fields'][2])]
   self.assertEqual(row['schema'],old['schema']);self.assertEqual(row['digests'][:3],old['digests'][:3])
   self.assertEqual(item['payload'],payload['payload']);self.assertEqual(row['fields'][14],4)
   self.assertEqual(joint['execution'][64+256*i:96+256*i],row['digests'][6])
 def test_old_platforms_remain_isolated(self):
  self.assertEqual(provider(ROOT,1)['namespaces'],[1,3]);self.assertEqual(provider(ROOT,2)['namespaces'],[2])
  self.assertEqual(len(firmware_inputs(ROOT,1)['entries']),13);self.assertEqual(len(firmware_inputs(ROOT,2)['entries']),4)
 def test_seventeenth_entry_is_rejected_without_raising_limit(self):
  from scripts.unified.build_catalog import build_catalog
  rows=catalog_entries(self.joint()['catalog'])
  with self.assertRaisesRegex(ValueError,'count'):build_catalog(rows+[rows[0]])
if __name__=='__main__':unittest.main()
