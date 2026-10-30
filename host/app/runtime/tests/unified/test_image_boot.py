"""Independent small FIT fixtures; not an ARM64 image or boot PASS."""
import struct,hashlib,gzip,unittest
from scripts.unified.audit_image import fit_payloads
def string(v):return v.encode()+b'\0'
def cells(*v):return struct.pack('>'+str(len(v))+'I',*v)
def flatten(nodes):
 strings=b'';offset={}
 for name in sorted({k for p in nodes.values() for k in p}):offset[name]=len(strings);strings+=string(name)
 block=b''
 def padded(raw):return raw+b'\0'*((-len(raw))%4)
 def emit(path):
  nonlocal block
  block+=cells(1)+padded(string(path.rsplit('/',1)[-1]))
  for k,v in nodes[path].items():block+=cells(3,len(v),offset[k])+padded(v)
  for child in nodes:
   if child!=path and child.rsplit('/',1)[0]==path.rstrip('/'):emit(child)
  block+=cells(2)
 emit('/');block+=cells(9);start=56;total=start+len(block)+len(strings)
 return struct.pack('>10I',0xd00dfeed,total,start,start+len(block),40,17,16,0,len(strings),len(block))+bytes(16)+block+strings
def fixture(change=None):
 kernel=gzip.compress(b'EXPLICIT_UNIT_KERNEL');dt=b'EXPLICIT_UNIT_DT';ramdisk=gzip.compress(b'EXPLICIT_UNIT_CPIO')
 nodes={'/':{},'/images':{},'/configurations':{'default':string('conf@system-top.dtb')},'/configurations/conf@system-top.dtb':{'kernel':string('kernel@1'),'fdt':string('fdt@system-top.dtb'),'ramdisk':string('ramdisk@1')}}
 for name,kind,compression,data in [('kernel@1','kernel','gzip',kernel),('fdt@system-top.dtb','flat_dt','none',dt),('ramdisk@1','ramdisk','gzip',ramdisk)]:
  path='/images/'+name;nodes[path]={'type':string(kind),'arch':string('arm64'),'compression':string(compression),'data':data}
  if kind in ('kernel','ramdisk'):nodes[path]['os']=string('linux')
  if kind=='kernel':nodes[path].update(load=cells(0x80000),entry=cells(0x80000))
  nodes[path+'/hash@1']={'algo':string('sha256'),'value':hashlib.sha256(data).digest()}
 if change:change(nodes)
 return flatten(nodes)
class FitAdmission(unittest.TestCase):
 def test_petalinux_gzipped_initramfs_is_passed_to_linux_without_uboot_decompression(self):
  # Actual 1504 FIT/1520 ITS: ramdisk data is .cpio.gz, compression=none.
  got=fit_payloads(fixture(lambda n:n['/images/ramdisk@1'].update(compression=string('none'))))
  self.assertEqual(gzip.decompress(got['rootfs_gzip']),b'EXPLICIT_UNIT_CPIO')
 def test_unsupported_metadata_or_plain_initramfs_cannot_bypass_content_checks(self):
  def plain(n):
   n['/images/ramdisk@1'].update(data=b'PLAIN_NOT_GZIP',compression=string('none'))
   n['/images/ramdisk@1/hash@1']['value']=hashlib.sha256(b'PLAIN_NOT_GZIP').digest()
  for change in (plain,lambda n:n['/images/ramdisk@1'].update(compression=string('lzma')),lambda n:n['/images/ramdisk@1'].update(arch=string('arm'))):
   with self.subTest(change=change),self.assertRaises(ValueError):fit_payloads(fixture(change))
 def test_extracts_selected_three_payloads_only_after_hash_and_config_check(self):
  got=fit_payloads(fixture());self.assertEqual(got['kernel'],b'EXPLICIT_UNIT_KERNEL');self.assertEqual(got['dtb'],b'EXPLICIT_UNIT_DT');self.assertEqual(gzip.decompress(got['rootfs_gzip']),b'EXPLICIT_UNIT_CPIO')
 def test_tampered_payload_hash_reference_or_weak_hash_rejected(self):
  changes=[lambda n:n['/images/kernel@1'].update(data=b'changed'),lambda n:n['/images/kernel@1/hash@1'].update(value=bytes(32)),lambda n:n['/configurations/conf@system-top.dtb'].update(kernel=string('other')),lambda n:n['/images/ramdisk@1/hash@1'].update(algo=string('sha1')),lambda n:n['/images/kernel@1'].update(entry=cells(0x10000000))]
  for change in changes:
   with self.subTest(change=change),self.assertRaises(ValueError):fit_payloads(fixture(change))
 def test_truncated_or_overlapping_binary_is_rejected(self):
  raw=fixture()
  for changed in (raw[:-1],raw[:8]+cells(0xfffffffc)+raw[12:]):
   with self.assertRaises(ValueError):fit_payloads(changed)
if __name__=='__main__':unittest.main()
