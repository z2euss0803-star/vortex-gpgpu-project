import hashlib,importlib.util,json,pathlib,tempfile,unittest
import numpy as np
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[2]
class PageAssets(unittest.TestCase):
 def test_full_source_identity_dimensions_and_reviewed_order(self):
  p=ROOT/'scripts/unified/build_hunet_fixed_inputs.py';s=importlib.util.spec_from_file_location('page_assets_tested',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
  with tempfile.TemporaryDirectory() as tmp:
   b=pathlib.Path(tmp);ref=b/'reference.py';ref.write_bytes(b'frozen fixture reference\n');m.CONVERTER_SHA=hashlib.sha256(ref.read_bytes()).hexdigest()
   source=b/'page.png';Image.new('RGB',(17,29),(23,45,67)).save(source);raw=source.read_bytes();pin=hashlib.sha256(raw).hexdigest()
   crop=b/'crop.png';Image.new('RGB',(3,5),(100,101,102)).save(crop);cr=crop.read_bytes();cp=hashlib.sha256(cr).hexdigest()
   tensor=np.zeros((2,3,128,128),dtype='<f4');tensor[1]=.25;t=b/'input.npy';np.save(t,tensor);tr=t.read_bytes()
   records=[]
   for i,name in enumerate(('p1_002','p1_001')):
    records.append(dict(id=name,page=1,status='include',review='accepted',source=dict(path=str(source),bytes=len(raw),sha256=pin),bbox_xyxy=[i,1,i+3,6],crop=dict(path=str(crop),bytes=len(cr),sha256=cp),preprocess=dict(tensor_sha256=hashlib.sha256(tensor[i].tobytes()).hexdigest())))
   data=dict(coverage_status='USER_REVIEWED',preprocess='P0_author_binary',records=records,tensors=dict(path=str(t),bytes=len(tr),sha256=hashlib.sha256(tr).hexdigest()));mf=b/'roi.json';mr=json.dumps(data).encode();mf.write_bytes(mr)
   report=m.build(mf,b/'out',hashlib.sha256(mr).hexdigest(),include_page_sources=True,reference_converter=ref)
   payload=b/'out/payload';catalog=json.loads((payload/'manifest.json').read_bytes());self.assertEqual((catalog['format'],catalog['version']),('hunet-fixed-inputs-v2',2));page=catalog['pages'][0]
   self.assertEqual([x['roi_id'] for x in page['items']],['p1_002','p1_001']);self.assertEqual(page['source_sha256'],pin)
   image=page['image'];self.assertEqual((image['width'],image['height'],image['stride']),(17,29,51));self.assertEqual((payload/image['source_file']).read_bytes(),raw)
   rgb=(payload/image['rgb_file']).read_bytes();self.assertEqual(rgb,Image.open(source).convert('RGB').tobytes());self.assertEqual(hashlib.sha256(rgb).hexdigest(),image['rgb_sha256']);self.assertEqual(len(rgb),image['rgb_bytes']);self.assertEqual(report['files'],8)
   data['records'][0]['source']['sha256']='0'*64;mr=json.dumps(data).encode();mf.write_bytes(mr)
   with self.assertRaises(ValueError):m.build(mf,b/'bad-source',hashlib.sha256(mr).hexdigest(),include_page_sources=True,reference_converter=ref)
   self.assertFalse((b/'bad-source/payload/manifest.json').exists())
   data['records'][0]['source']['sha256']=pin;data['records'][0]['bbox_xyxy']=[0,0,18,5];mr=json.dumps(data).encode();mf.write_bytes(mr)
   with self.assertRaises(ValueError):m.build(mf,b/'bad-box',hashlib.sha256(mr).hexdigest(),include_page_sources=True,reference_converter=ref)
if __name__=='__main__':unittest.main()
