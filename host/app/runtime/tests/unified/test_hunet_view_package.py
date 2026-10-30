import pathlib,tempfile,unittest,json,hashlib,shutil,os
from scripts.unified.prepare_hunet_view import prepare
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ViewPackage(unittest.TestCase):
 def setUp(self):
  base=ROOT/'build';base.mkdir(exist_ok=True);self.temp=pathlib.Path(tempfile.mkdtemp(prefix='hunet-view-',dir=str(base))).resolve();assert self.temp.parent==base.resolve()
 def tearDown(self):
  assert self.temp.parent==(ROOT/'build').resolve() and self.temp.name.startswith('hunet-view-')
  for p in sorted(self.temp.rglob('*'),key=lambda p:len(p.parts),reverse=True):
   if p.is_dir() and not p.is_symlink():p.rmdir()
   else:p.chmod(0o600);p.unlink()
  self.temp.rmdir()
 def test_exact_frozen_bundle_and_no_inference_claim(self):
  out=self.temp/'view';r=prepare(ROOT/'data/demo',out)
  self.assertEqual(r.get('scope'),'SEALED_HUNET_VIEW_ASSETS_ONLY_NOT_INFERENCE_OR_DP','actual HUNet view asset bundle must retain frozen identities')
  manifest=(out/'manifest.json').read_bytes();self.assertEqual(r['sha256'],hashlib.sha256(manifest).hexdigest());m=json.loads(manifest)
  self.assertEqual(m['format'],'hunet-view-v1');self.assertEqual(m['source'],dict(width=474,height=345,stride=1422,layout='RGB888',roi=[40,12,87,105],roi_index=0))
  self.assertEqual(m['input_sha256'],'de5513346d7922032e415b2d5eb40a5862b084a2ed264b73602ab8f00769f0de')
  self.assertEqual(m['classes_sha256'],'ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f');self.assertEqual(m['font_sha256'],'763146584cf0710223441356b4395e279021b0806c196614377a7a0174ae074a')
  self.assertIsNone(m['gpu_raw_cycles']);self.assertEqual(m['numeric_policy'],'PRESERVE_PRODUCER_EVALUATION_AND_HISTORY')
  expected={'source.rgb':(490590,'6daf55f02b1bbae91b26b197b374f4e102765a53b733c1c30cc47b4439d4124a'),'labels.bin':(259456,'3afa8a848bedd4d259f205cbf73eaf1a9b27c1f88d00831d99e16ee2af5167e9'),'glyphs-u8.bin':(8299520,'9c4ed43ae523d6cd1807ff5763e6bb281dad8cd094af79d0ff0474cd1063f32c')}
  self.assertEqual({x['path'] for x in m['files']},set(expected))
  for row in m['files']:
   b=(out/row['path']).read_bytes();self.assertEqual((len(b),hashlib.sha256(b).hexdigest()),expected[row['path']]);self.assertEqual(row['bytes'],len(b));self.assertEqual(row['sha256'],expected[row['path']][1])
  self.assertEqual((out/'manifest.sha256').read_text(),r['sha256']+'\n')
 def test_existing_directory_never_overwritten(self):
  out=self.temp/'existing';out.mkdir();p=out/'keep.bin';p.write_bytes(b'original')
  with self.assertRaises(FileExistsError):prepare(ROOT/'data/demo',out)
  self.assertEqual(p.read_bytes(),b'original')
 def test_changed_frozen_input_rejected_before_output(self):
  src=self.temp/'src';src.mkdir()
  for p in (ROOT/'data/demo').iterdir():
   if p.is_file():shutil.copyfile(str(p),str(src/p.name))
  p=src/'hunet-input-roi0.f32';b=bytearray(p.read_bytes());b[0]^=1;p.write_bytes(b)
  with self.assertRaises(ValueError):prepare(src,self.temp/'view')
  self.assertFalse((self.temp/'view').exists())
if __name__=='__main__':unittest.main(verbosity=2)
