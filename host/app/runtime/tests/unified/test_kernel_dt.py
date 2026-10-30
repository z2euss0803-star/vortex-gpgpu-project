"""Hand-declared FDT fixtures test platform admission, not a compiled DT or GPU."""
import struct,unittest
from scripts.unified.audit_kernel import audit_device_tree

def cells(*values):return struct.pack('>'+str(len(values))+'I',*values)
def string(value):return value.encode('ascii')+b'\0'

def fixture(change=None):
 # These addresses/digests are explicit expected inputs, not auditor output.
 nodes={
 '/':{'#address-cells':cells(2),'#size-cells':cells(2)},
 '/reserved-memory':{'#address-cells':cells(2),'#size-cells':cells(2),'ranges':b''},
 '/reserved-memory/vortex-ps@10000000':{'compatible':string('shared-dma-pool'),'reg':cells(0,0x10000000,0,0x100000),'no-map':b'','phandle':cells(1)},
 '/amba_pl@0':{'#address-cells':cells(2),'#size-cells':cells(2),'ranges':b''},
 '/amba_pl@0/HPM@a0000000':{'reg':cells(0,0xa0000000,0,0x10000),'status':string('disabled')},
 '/amba_pl@0/dma@b0010000':{'reg':cells(0,0xb0010000,0,0x10000),'status':string('disabled')},
 '/amba_pl@0/gpio@b0020000':{'reg':cells(0,0xb0020000,0,0x10000),'#gpio-cells':cells(3),'phandle':cells(2),'status':string('okay')},
 '/firmware':{},'/firmware/clock-controller':{'phandle':cells(3),'#clock-cells':cells(1)},
 '/amba_pl@0/vortex-backend@a0000000':{
  'compatible':string('vortex,hw4-s8-demo-v1'),'status':string('okay'),
  'reg':cells(0,0xa0000000,0,0x10000,0,0xb0010000,0,0x10000),
  'reg-names':b'gpu\0cdma\0','clocks':cells(3,71),'clock-names':string('axi'),
  'ready-gpios':cells(2,0,0,0),'memory-region':cells(1),
  'vortex,compute-v2':b'','vortex,ps-placement-approved':b'',
  'vortex,hardware-contract-sha256':bytes.fromhex('11'*32),
  'vortex,bit-sha256':bytes.fromhex('22'*32),'vortex,xsa-sha256':bytes.fromhex('33'*32),
  'vortex,catalog-sha256':bytes.fromhex('44'*32),'vortex,execution-sha256':bytes.fromhex('55'*32)}}
 if change:change(nodes)
 names=sorted({name for p in nodes.values() for name in p});strings=b'';offset={}
 for name in names:offset[name]=len(strings);strings+=string(name)
 block=b''
 def padded(raw):return raw+b'\0'*((-len(raw))%4)
 def emit(path):
  nonlocal block
  block+=cells(1)+padded(string(path.rsplit('/',1)[-1]))
  for name,value in nodes[path].items():block+=cells(3,len(value),offset[name])+padded(value)
  for child in nodes:
   if child!=path and child.rsplit('/',1)[0]==path.rstrip('/'):emit(child)
  block+=cells(2)
 emit('/');block+=cells(9);reserve=b'\0'*16;start=40+len(reserve);total=start+len(block)+len(strings)
 return struct.pack('>10I',0xd00dfeed,total,start,start+len(block),40,17,16,0,len(strings),len(block))+reserve+block+strings

PROVIDER=dict(provider_flags=1,compatible='vortex,hw4-s8-demo-v1',hardware_contract='11'*32,bit_sha256='22'*32,xsa_sha256='33'*32,ps_base=0x10000000,ps_bytes=0x100000,pl_base=0x500000000,pl_bytes=0x80000000)
BACKEND='/amba_pl@0/vortex-backend@a0000000'
def audit(raw):return audit_device_tree(raw,PROVIDER,'44'*32,'55'*32)

class DeviceTreeAdmission(unittest.TestCase):
 def test_exact_binary_binding_has_declared_not_measured_identity(self):
  row=audit(fixture())
  self.assertEqual(row.get('backend'),BACKEND)
  self.assertEqual(row.get('provider_flags'),1)
  self.assertEqual(row.get('ps_pool'),[0x10000000,0x100000])
  self.assertIs(row.get('hardware_hash_measured'),False)
  self.assertIsNone(row.get('gpu_raw_cycles'))
 def test_cross_provider_is_rejected(self):
  with self.assertRaises(ValueError):audit(fixture(lambda n:n[BACKEND].update(compatible=string('vortex,w4-graphics-p8-v1'))))
 def test_explicit_v2_and_placement_approval_are_required(self):
  for key in ('vortex,compute-v2','vortex,ps-placement-approved'):
   with self.subTest(key=key),self.assertRaises(ValueError):audit(fixture(lambda n:n[BACKEND].pop(key)))
 def test_every_binding_digest_is_exact_and_32_bytes(self):
  for key in ('hardware-contract','bit','xsa','catalog','execution'):
   with self.subTest(key=key),self.assertRaises(ValueError):audit(fixture(lambda n:n[BACKEND].update({'vortex,'+key+'-sha256':b'\x00'*31})))
  with self.assertRaises(ValueError):audit_device_tree(fixture(),PROVIDER,'66'*32,'55'*32)
 def test_wrong_gpu_or_cdma_extent_is_rejected(self):
  with self.assertRaises(ValueError):audit(fixture(lambda n:n[BACKEND].update(reg=cells(0,0xa0000000,0,0x10000,0,0xb0010000,0,0x20000))))
 def test_reserved_pool_phandle_and_no_map_are_required(self):
  for change in (lambda n:n[BACKEND].update({'memory-region':cells(99)}),lambda n:n['/reserved-memory/vortex-ps@10000000'].pop('no-map'),lambda n:n['/reserved-memory/vortex-ps@10000000'].update(reg=cells(0,0x10000000,0,0x200000))):
   with self.assertRaises(ValueError):audit(fixture(change))
 def test_other_enabled_owner_of_cdma_is_rejected(self):
  with self.assertRaises(ValueError):audit(fixture(lambda n:n['/amba_pl@0/dma@b0010000'].update(status=string('okay'))))
 def test_ready_gpio_and_clock_references_must_resolve(self):
  for prop,value in [('ready-gpios',cells(90,0,0,0)),('clocks',cells(91,71)),('clocks',cells(3,72))]:
   with self.subTest(prop=prop),self.assertRaises(ValueError):audit(fixture(lambda n:n[BACKEND].update({prop:value})))
 def test_truncation_overlap_and_duplicate_phandles_are_rejected(self):
  good=fixture()
  for raw in (good[:-1],good[:8]+cells(40)+good[12:],good[:4]+cells(0xffffffff)+good[8:]):
   with self.assertRaises(ValueError):audit(raw)
  with self.assertRaises(ValueError):audit(fixture(lambda n:n['/amba_pl@0/gpio@b0020000'].update(phandle=cells(1))))
 def test_referenced_dma_gpio_and_clock_devices_must_be_enabled(self):
  for path in ('/reserved-memory/vortex-ps@10000000','/amba_pl@0/gpio@b0020000','/firmware/clock-controller'):
   with self.subTest(path=path),self.assertRaises(ValueError):audit(fixture(lambda n:n[path].update(status=string('disabled'))))
 def test_reserved_parent_cell_width_cannot_reinterpret_pool(self):
  with self.assertRaises(ValueError):audit(fixture(lambda n:n['/reserved-memory'].update({'#address-cells':cells(1)})))

if __name__=='__main__':unittest.main()
