import copy,hashlib,json,pathlib,struct,tempfile,unittest,os,stat
from unittest import mock
from scripts.unified.texture_package import build_package

class Package(unittest.TestCase):
 def test_native_origin_mismatch_is_not_ignored(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);meta,text,raw=self.fixture(root/'source')
   meta['native_origin']={'format':'unified-native-mosaic-origin-v1','evidence_scope':'REAL_BOARD'}
   (root/'source/result.json').write_text(json.dumps(meta))
   with self.assertRaises(ValueError):build_package(root/'source/result.json',root/'package')
   self.assertFalse((root/'package').exists(),'unverified native provenance must not generate a texture package')
 def fixture(self,root):
  meta=json.loads(pathlib.Path('tests/demo/fixtures/run-v1.json').read_bytes());raw=b''.join(struct.pack('<f',x)*64 for x in (-1.,127.75,300.));meta['artifacts']=[dict(role='output',name='output.f32',dtype='FP32',bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),shape=[1,3,8,8])];text=(json.dumps(meta,sort_keys=True)+'\n').encode();root.mkdir();(root/'result.json').write_bytes(text);(root/'output.f32').write_bytes(raw);return meta,text,raw
 def test_build_mosaic_dual_lod_without_upgrading_evidence(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);meta,text,raw=self.fixture(root/'source');out=root/'package';result=build_package(root/'source/result.json',out);self.assertIsNotNone(result,'Mosaic dual-LOD package must be built');self.assertEqual(result['schema'],'vortex-result-texture-v1');self.assertEqual(result['source']['scope'],'OFFLINE');self.assertIsNone(result['gpu_raw_cycles']);self.assertEqual(result['source']['numeric_class'],'NOT_EVALUATED');self.assertEqual((out/result['source']['result_json']).read_bytes(),text);self.assertEqual((out/result['source']['tensor']).read_bytes(),raw);self.assertEqual(result['source']['tensor_sha256'],hashlib.sha256(raw).hexdigest());self.assertEqual((root/'source/result.json').read_bytes(),text);self.assertEqual((root/'source/output.f32').read_bytes(),raw)
   self.assertEqual(sum(x['bytes'] for x in result['lods']),17825792);self.assertEqual([x['width'] for x in result['lods']],[512,2048]);self.assertEqual(result['preprocessing']['policy'],'cover-visible-mean-v1');self.assertFalse(result['visual_acceptance']);self.assertFalse(result['gpu_execution_acceptance'])
   for row in result['lods']:
    data=(out/row['name']).read_bytes();self.assertEqual(len(data),row['width']**2*4);self.assertEqual(data,bytes((0,127,255,255))*(row['width']**2));self.assertEqual(hashlib.sha256(data).hexdigest(),row['sha256'])
 def test_invalid_sources_do_not_create_destination(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);meta,text,raw=self.fixture(root/'source');out=root/'package'
   for kind in ('incomplete','hash','nonfinite','scope-duplicate','escape'):
    candidate=copy.deepcopy(meta);data=raw
    if kind=='incomplete':candidate['state']='RUNNING'
    if kind=='hash':data=bytes((raw[0]^1,))+raw[1:]
    if kind=='nonfinite':data=struct.pack('<I',0x7fc00000)+raw[4:];candidate['artifacts'][0]['sha256']=hashlib.sha256(data).hexdigest()
    if kind=='escape':candidate['artifacts'][0]['name']='../output.f32'
    encoded=json.dumps(candidate)
    if kind=='scope-duplicate':encoded=encoded[:-1]+',"scope":"REAL_BOARD"}'
    (root/'source/result.json').write_text(encoded);(root/'source/output.f32').write_bytes(data)
    with self.assertRaises((ValueError,OSError)):build_package(root/'source/result.json',out)
    self.assertFalse(out.exists())
 def test_payload_directories_durable_before_manifest_publication(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);self.fixture(root/'source');out=root/'package';synced=set();real_sync=os.fsync;real_replace=os.replace
   def sync(fd):
    if stat.S_ISDIR(os.fstat(fd).st_mode):synced.add(pathlib.Path(os.readlink('/proc/self/fd/%d'%fd)).resolve())
    return real_sync(fd)
   def replace(source,target):
    self.assertTrue({out.resolve(),(out/'source').resolve(),(out/'derived').resolve(),root.resolve()}<=synced,'all payload directories durable before manifest publication');return real_replace(source,target)
   with mock.patch('scripts.unified.texture_package.os.fsync',side_effect=sync),mock.patch('scripts.unified.texture_package.os.replace',side_effect=replace):build_package(root/'source/result.json',out)
 def test_payload_file_sync_failure_never_publishes_manifest(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);meta,text,raw=self.fixture(root/'source');out=root/'package';real_sync=os.fsync
   def fail(fd):
    if stat.S_ISREG(os.fstat(fd).st_mode):raise OSError(5,'injected payload fsync error')
    return real_sync(fd)
   with mock.patch('scripts.unified.texture_package.os.fsync',side_effect=fail):
    with self.assertRaises(OSError):build_package(root/'source/result.json',out)
   self.assertFalse((out/'manifest.json').exists());self.assertEqual((root/'source/result.json').read_bytes(),text);self.assertEqual((root/'source/output.f32').read_bytes(),raw)
if __name__=='__main__':unittest.main()
