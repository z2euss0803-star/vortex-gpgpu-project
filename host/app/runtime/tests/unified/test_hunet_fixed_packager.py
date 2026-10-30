import copy,hashlib,importlib.util,json,pathlib,tempfile,unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
ORIGINAL=ROOT.parent/'hunet_original_tcu_20261008_v1/data/roi_manifest.json'

class FixedPagePackager(unittest.TestCase):
 def module(self):
  path=ROOT/'scripts/unified/build_hunet_fixed_inputs.py'
  self.assertTrue(path.is_file(),'reviewed-page fixed-input packager required')
  spec=importlib.util.spec_from_file_location('fixed_hunet_inputs_tested',str(path));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
 def fixture(self,base):
  original=json.loads(ORIGINAL.read_bytes());selected=[copy.deepcopy(x) for x in original['records'] if x['id'] in ('p1_001','p1_002')]
  rejected=copy.deepcopy(selected[0]);rejected.update(id='p1_099',status='truncated');selected.append(rejected)
  import numpy as np
  tensor_path=pathlib.Path(original['tensors']['path']);all_tensors=np.load(tensor_path,allow_pickle=False);indices=[next(i for i,x in enumerate(original['records']) if x['id']==roi) for roi in ('p1_001','p1_002','p1_001')]
  subset=base/'frozen.npy';np.save(subset,all_tensors[indices]);content=subset.read_bytes();original['tensors']=dict(path=str(subset),bytes=len(content),sha256=hashlib.sha256(content).hexdigest())
  original['records']=selected;p=base/'reviewed.json';raw=json.dumps(original).encode();p.write_bytes(raw);return p,hashlib.sha256(raw).hexdigest(),selected
 def test_exact_frozen_crops_and_tensors_only_include_reviewed_boxes(self):
  module=self.module()
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp);manifest,pin,rows=self.fixture(base);out=base/'result';result=module.build(manifest,out,pin)
   self.assertEqual(result['included'],2);self.assertEqual(result['excluded'],1)
   payload=out/'payload';raw=(payload/'manifest.json').read_bytes();data=json.loads(raw)
   self.assertEqual((payload/'manifest.sha256').read_text().strip(),hashlib.sha256(raw).hexdigest())
   self.assertLessEqual(len(raw),65536);items=data['pages'][0]['items'];self.assertEqual([r['roi_id'] for r in items],['p1_001','p1_002'])
   for row,expected in zip(items,rows):
    self.assertEqual(row['tensor_sha256'],expected['preprocess']['tensor_sha256']);self.assertEqual(row['source_sha256'],expected['crop']['sha256'])
    self.assertEqual(hashlib.sha256((payload/row['tensor_file']).read_bytes()).hexdigest(),row['tensor_sha256']);self.assertEqual(hashlib.sha256((payload/row['source_file']).read_bytes()).hexdigest(),row['source_sha256'])
   self.assertEqual(len(list(payload.iterdir())),6)
 def test_crop_identity_drift_is_rejected_before_a_valid_manifest_is_published(self):
  module=self.module()
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp);path,pin,rows=self.fixture(base);data=json.loads(path.read_bytes());data['records'][0]['crop']['sha256']='0'*64;raw=json.dumps(data).encode();path.write_bytes(raw)
   with self.assertRaises(ValueError):module.build(path,base/'bad',hashlib.sha256(raw).hexdigest())
   self.assertFalse((base/'bad/payload/manifest.json').exists())
 def test_frozen_tensor_mismatch_is_not_recalibrated_or_accepted(self):
  module=self.module()
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp);path,pin,rows=self.fixture(base);data=json.loads(path.read_bytes());data['records'][0]['preprocess']['tensor_sha256']='0'*64;raw=json.dumps(data).encode();path.write_bytes(raw)
   with self.assertRaises(ValueError):module.build(path,base/'bad',hashlib.sha256(raw).hexdigest())
   self.assertFalse((base/'bad/payload/manifest.json').exists())
 def test_existing_output_and_wrong_source_manifest_pin_are_refused(self):
  module=self.module()
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp);path,pin,rows=self.fixture(base);existing=base/'existing';existing.mkdir();(existing/'keep').write_text('old')
   with self.assertRaises(FileExistsError):module.build(path,existing,pin)
   self.assertEqual((existing/'keep').read_text(),'old')
   with self.assertRaises(ValueError):module.build(path,base/'untrusted','0'*64)
   self.assertFalse((base/'untrusted/payload/manifest.json').exists())

if __name__=='__main__':unittest.main()
