import hashlib,json,pathlib,struct,subprocess,tempfile,unittest
from scripts.hunet_opt.package_release import source_inventory,validate_payload,make_binding,validate_cross,release_header,hardware_from_verified,write_runtime_assets,check_runtime_assets
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Release(unittest.TestCase):
 def test_mandatory_cli_libm_asset(self):
  with self.temp() as d:
   p=pathlib.Path(d);(p/'cpu').mkdir();runtime=dict(libm_sha256='a'*64)
   with self.assertRaises((ValueError,FileNotFoundError)):check_runtime_assets(p,runtime)
   write_runtime_assets(p,runtime);self.assertEqual((p/'cpu/libm.sha256').read_bytes(),b'a'*64+b'\n')
   self.assertEqual(check_runtime_assets(p,runtime),'PASS')
   for bad in (b'b'*64+b'\n',b'a'*64,b'a'*64+b'\r\n'):
    (p/'cpu/libm.sha256').write_bytes(bad)
    with self.assertRaises(ValueError):check_runtime_assets(p,runtime)
 def test_legacy_verifier_result_is_not_the_manifest(self):
  manifest=dict(bit_sha256='a'*64,xsa_sha256='b'*64)
  self.assertEqual(hardware_from_verified(dict(integrity='PASS'),manifest,manifest),manifest)
  with self.assertRaises(ValueError):hardware_from_verified(dict(integrity='PASS'),manifest,dict(manifest,bit_sha256='c'*64))
 def temp(self):
  p=ROOT/'build/test-tmp';p.mkdir(parents=True,exist_ok=True);return tempfile.TemporaryDirectory(dir=str(p))
 def test_release_clean_source_and_no_hash_cycle(self):
  with self.temp() as d:
   p=pathlib.Path(d);subprocess.run(['git','init','-q',str(p)],check=True);(p/'core').mkdir();(p/'core/a.c').write_text('int a;\n');subprocess.run(['git','-C',str(p),'add','core/a.c'],check=True)
   subprocess.run(['git','-C',str(p),'-c','user.name=Test','-c','user.email=test@localhost','commit','-qm','fixture'],check=True)
   i=source_inventory(p);self.assertIn('core/a.c',i['files']);(p/'core/a.c').write_text('int b;\n')
   with self.assertRaises(ValueError):source_inventory(p)
  hashes=[hashlib.sha256(bytes([i])).digest() for i in range(13)]
  b=make_binding(*hashes);self.assertEqual(len(b),512);self.assertEqual(struct.unpack_from('<8I',b),(0x44424c48,4,512,4,10,31,0,0))
  self.assertEqual(b,make_binding(*hashes));self.assertEqual(b[352:384],bytes(32));self.assertEqual(b[480:],bytes(32))
  header=release_header(b,dict(wire_bytes=64,immutable_spans=[dict(bytes=32,sha256='a'*64)],wire_sha256='b'*64))
  self.assertIn(b'vxl_opt_catalog_default()',header);self.assertNotIn(b'CPU_BINARY_SHA',header)
 def test_closed_payload_mutation_and_missing_source(self):
  with self.temp() as d:
   p=pathlib.Path(d);records={}
   for n in ('wire','cpu','descriptor','model'):
    raw=n.encode();(p/n).write_bytes(raw);records[n]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
   self.assertEqual(validate_payload(p,records),'PASS')
   for n in records:
    raw=(p/n).read_bytes();(p/n).write_bytes(raw+b'x')
    with self.assertRaises(ValueError):validate_payload(p,records)
    (p/n).write_bytes(raw)
   (p/'unexpected').write_bytes(b'x')
   with self.assertRaises(ValueError):validate_payload(p,records)
 def test_wrong_receipts_never_become_release(self):
  i=dict(source_commit='a'*40,source_digest='b'*64)
  for r in ({},{'scope':'HOST_SOFTWARE_NOT_GPU','exit':0,'audit':{'integrity':'PASS'}},{'scope':'OPT_CPU_AARCH64_BUILD','exit':0,'audit':{'integrity':'PASS'},'source_commit':'c'*40}):
   with self.assertRaises(ValueError):validate_cross(r,'cpu',i,'COMBINED')
if __name__=='__main__':unittest.main()
