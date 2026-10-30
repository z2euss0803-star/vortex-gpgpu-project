import hashlib,importlib.util,io,json,os,pathlib,shutil,subprocess,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2];BASE=ROOT.parent;SCRIPT=ROOT/'scripts/unified/build_hunet_fixed_inputs.py';CONVERTER=BASE/'hunet/reference/roi.py';spec=importlib.util.spec_from_file_location('asset_builder545',SCRIPT);builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
def digest(raw):return hashlib.sha256(raw).hexdigest()
class AssetPathMap(unittest.TestCase):
 def setUp(self):self.temp=tempfile.TemporaryDirectory(prefix='asset-map-',dir=BASE/'tmp');self.root=pathlib.Path(self.temp.name)
 def tearDown(self):self.temp.cleanup()
 def fixture(self,missing=False):
  import numpy as np
  from PIL import Image
  values=np.zeros((1,3,128,128),dtype='<f4');stream=io.BytesIO();np.save(stream,values,allow_pickle=False);npy=stream.getvalue();png=io.BytesIO();Image.new('RGB',(8,8),(0,0,0)).save(png,format='PNG');image=png.getvalue();payload={'tensor.npy':npy,'crop.png':image,'page.png':image};dest=self.root/'captured';dest.mkdir();original=self.root/'original'
  if not missing:original.mkdir()
  old={};mapping={}
  for name,raw in payload.items():
   (dest/name).write_bytes(raw);old[name]=('Z:/never-installed/hunet545/'+name) if missing else str(original/name)
   if not missing:(original/name).write_bytes(raw)
   mapping[old[name]]='captured/'+name
  data=dict(coverage_status='USER_REVIEWED',preprocess='P0_author_binary',records=[dict(id='p1_001',page=1,status='include',review='accepted',bbox_xyxy=[0,0,8,8],crop=dict(path=old['crop.png'],bytes=len(image),sha256=digest(image)),source=dict(path=old['page.png'],bytes=len(image),sha256=digest(image)),preprocess=dict(tensor_sha256=digest(values[0].tobytes())))],tensors=dict(path=old['tensor.npy'],bytes=len(npy),sha256=digest(npy)))
  manifest=self.root/'roi.json';raw=(json.dumps(data)+'\n').encode();manifest.write_bytes(raw);mapfile=self.root/'path-map.json';mapfile.write_bytes((json.dumps(mapping)+'\n').encode());return manifest,digest(raw),mapfile,mapping
 def call(self,manifest,expected,out,mapfile=None,mapsum=None,converter=CONVERTER):return builder.build(manifest,out,expected,include_page_sources=True,reference_converter=converter,source_path_map=mapfile,source_path_map_sha256=mapsum)
 def test_cli_relocates_missing_original_paths_without_editing_manifest(self):
  manifest,expected,mapfile,_=self.fixture(missing=True);out=self.root/'out';q=subprocess.run([sys.executable,'-B',str(SCRIPT),'--roi-manifest',str(manifest),'--roi-manifest-sha256',expected,'--out',str(out),'--include-page-sources','--reference-converter',str(CONVERTER),'--source-path-map',str(mapfile),'--source-path-map-sha256',digest(mapfile.read_bytes())],capture_output=True);self.assertEqual(q.returncode,0,q.stderr.decode(errors='replace'));report=json.loads(q.stdout);self.assertEqual((report['included'],report['files'],report['natural_exit']),(1,6,0));self.assertEqual(digest(manifest.read_bytes()),expected);self.assertEqual(report['source_path_map_sha256'],digest(mapfile.read_bytes()))
 def test_default_old_paths_and_relocated_payload_identical(self):
  manifest,expected,mapfile,_=self.fixture();old=self.root/'old';new=self.root/'new';a=builder.build(manifest,old,expected,include_page_sources=True,reference_converter=CONVERTER);b=self.call(manifest,expected,new,mapfile,digest(mapfile.read_bytes()));self.assertEqual(a['manifest_sha256'],b['manifest_sha256']);self.assertEqual({p.name:digest(p.read_bytes()) for p in (old/'payload').iterdir()},{p.name:digest(p.read_bytes()) for p in (new/'payload').iterdir()})
 def test_unsafe_nonstring_unknown_missing_invalid_maps_rejected(self):
  manifest,expected,mapfile,mapping=self.fixture();first=next(iter(mapping));cases=[]
  for bad in ['/absolute','C:/absolute','C:drive-relative','//server/share','../escape','a/../escape','','.','a//b','a\\b','captured/missing',None,7,True,{},[]]:cases.append(dict(mapping,**{first:bad}))
  cases.append(dict(mapping,**{first:'captured'+chr(92)+'tensor.npy'}))
  cases.append(dict(mapping,**{first:'captured'+chr(92)+'..'+chr(92)+'captured'+chr(92)+'tensor.npy'}))
  cases += [{k:v for k,v in mapping.items() if k!=first},dict(mapping,unknown='captured/tensor.npy'),dict(mapping,**{first:'captured'}),[],None]
  for index,case in enumerate(cases):
   with self.subTest(index=index):
    raw=json.dumps(case).encode();mapfile.write_bytes(raw);out=self.root/('out-'+str(index))
    with self.assertRaises(ValueError):self.call(manifest,expected,out,mapfile,digest(raw))
    self.assertFalse(out.exists())
 def test_duplicate_keys_rejected_even_with_correct_map_sha(self):
  manifest,expected,mapfile,mapping=self.fixture();pairs=list(mapping.items());raw=('{'+','.join(json.dumps(k)+':'+json.dumps(v) for k,v in pairs+[pairs[0]])+'}').encode();mapfile.write_bytes(raw)
  with self.assertRaisesRegex(ValueError,'duplicate'):self.call(manifest,expected,self.root/'out',mapfile,digest(raw))
 def test_map_and_sha_required_together_and_sha_must_match(self):
  manifest,expected,mapfile,_=self.fixture()
  for index,(path,mapsum) in enumerate([(mapfile,None),(None,digest(mapfile.read_bytes())),(mapfile,'0'*64),(mapfile,'invalid')]):
   with self.subTest(index=index):
    out=self.root/('out-'+str(index))
    with self.assertRaises(ValueError):self.call(manifest,expected,out,path,mapsum)
    self.assertFalse(out.exists())
 def test_real_symlink_target_rejected(self):
  inside=self.root/'inside';inside.mkdir();outside=self.root/'outside.bin';outside.write_bytes(b'outside');(inside/'normal.bin').write_bytes(b'normal');link=inside/'alias.bin'
  try:link.symlink_to(outside)
  except OSError as error:self.skipTest('local account cannot create symlink: '+str(error))
  data=dict(tensors=dict(path='old-tensor'),records=[dict(crop=dict(path='old-crop'),source=dict(path='old-page'))]);mapping={'old-tensor':'alias.bin','old-crop':'normal.bin','old-page':'normal.bin'};path=inside/'map.json';raw=json.dumps(mapping).encode();path.write_bytes(raw)
  with self.assertRaisesRegex(ValueError,'symlink'):builder.source_path_resolver(data,path,digest(raw))
 def test_original_array_identity_check_still_rejects_mapped_drift(self):
  manifest,expected,mapfile,_=self.fixture();p=self.root/'captured/tensor.npy';raw=bytearray(p.read_bytes());raw[-1]^=1;p.write_bytes(raw)
  with self.assertRaisesRegex(ValueError,'tensor array identity'):self.call(manifest,expected,self.root/'out',mapfile,digest(mapfile.read_bytes()))
 def test_original_crop_and_page_identity_checks_still_reject_mapped_drift(self):
  manifest,expected,mapfile,_=self.fixture()
  for index,name in enumerate(['crop.png','page.png']):
   p=self.root/'captured'/name;original=p.read_bytes();raw=bytearray(original);raw[-1]^=1;p.write_bytes(raw)
   with self.assertRaisesRegex(ValueError,'identity'):self.call(manifest,expected,self.root/('out-'+str(index)),mapfile,digest(mapfile.read_bytes()))
   p.write_bytes(original)
 def test_original_converter_pin_still_required(self):
  manifest,expected,mapfile,_=self.fixture();converter=self.root/'bad.py';converter.write_bytes(b'# changed\n')
  with self.assertRaisesRegex(ValueError,'reference changed'):self.call(manifest,expected,self.root/'out',mapfile,digest(mapfile.read_bytes()),converter)
if __name__=='__main__':unittest.main()
