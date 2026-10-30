import json,os,shutil,sys,tempfile,unittest,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from package_release import verify_release,package_release,safe_name
def collision(original):
 b=bytearray(original);b[0]^=1;b[-4:]=bytes(4);base=zlib.crc32(b);basis={}
 for i in range(32):
  c=bytearray(b);c[-4+i//8]^=1<<(i%8);v=zlib.crc32(c)^base;mask=1<<i
  for bit in range(31,-1,-1):
   if not v&(1<<bit):continue
   if bit in basis:v^=basis[bit][0];mask^=basis[bit][1]
   else:basis[bit]=(v,mask);break
 v=zlib.crc32(original)^base;patch=0
 for bit in range(31,-1,-1):
  if v&(1<<bit):v^=basis[bit][0];patch^=basis[bit][1]
 if v:raise AssertionError('CRC basis')
 b[-4:]=patch.to_bytes(4,'little');return bytes(b)
class Release(unittest.TestCase):
 def test_reference_bytes_cannot_be_rebound_by_outer_file_list(self):
  import hashlib
  src=Path((ROOT/'release/CURRENT').read_text(encoding='utf-8').strip());tmp=ROOT/'build/test-tmp';tmp.mkdir(parents=True,exist_ok=True)
  for name in ['references/acceptance.json','references/roi0.bare.f32']:
   with tempfile.TemporaryDirectory(dir=tmp) as t:
    dst=Path(t)/'candidate';shutil.copytree(src,dst);p=dst/name;raw=p.read_bytes()
    if name.endswith('.json'):
     v=json.loads(raw);v['device_logits_R2']['atol']=1000;raw=json.dumps(v).encode()
    else:
     v=bytearray(raw);v[0]^=1;raw=bytes(v)
    p.write_bytes(raw);r=json.loads((dst/'release.json').read_text());r['files'][name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
    refs=json.loads((dst/'references.json').read_text());record=refs['original_acceptance'] if name.endswith('.json') else refs['v5']['0'];record.update(r['files'][name])
    data=json.dumps(refs).encode();(dst/'references.json').write_bytes(data);r['files']['references.json']=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest());(dst/'release.json').write_text(json.dumps(r))
    self.assertEqual(verify_release(dst)['integrity'],'FAIL',name)

 def test_uncommitted_or_wrong_commit_rejected(self):self.assertRaises(ValueError,package_release,ROOT,ROOT/'build/forbidden-package','00'*20)
 def test_unsafe_names(self):
  for name in ['../x','/x','a/../../x','C:/x','x\\y','core/v6_kernel.c']:
   self.assertRaises(ValueError,safe_name,name)
 def test_real_release_and_tamper(self):
  src=Path(os.environ.get('HUNET_RELEASE',(ROOT/'release/CURRENT').read_text(encoding='utf-8').strip()))
  self.assertEqual(verify_release(src)['integrity'],'PASS')
  tmp=ROOT/'build/test-tmp';tmp.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=tmp) as t:
   dst=Path(t)/'candidate';shutil.copytree(src,dst)
   p=dst/'program6/code.bin';b=bytearray(p.read_bytes());b[0]^=1;p.write_bytes(b)
   self.assertEqual(verify_release(dst)['integrity'],'FAIL')
 def test_symlink_and_crc_only_rejected(self):
  src=Path((ROOT/'release/CURRENT').read_text(encoding='utf-8').strip());self.assertEqual(verify_release(src)['integrity'],'PASS');tmp=ROOT/'build/test-tmp';tmp.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=tmp) as t:
   dst=Path(t)/'candidate';shutil.copytree(src,dst);p=dst/'program6/code.bin';old=p.read_bytes();bad=collision(old);self.assertNotEqual(bad,old);self.assertEqual(zlib.crc32(old),zlib.crc32(bad));p.write_bytes(bad);self.assertEqual(verify_release(dst)['integrity'],'FAIL');p.unlink();p.symlink_to((src/'program6/code.bin').resolve());self.assertEqual(verify_release(dst)['integrity'],'FAIL')
if __name__=='__main__':unittest.main()
