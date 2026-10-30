"""Package existing reviewed HUNet crops; no detector, calibration, inference or device action."""
import argparse,hashlib,importlib.util,json,os,pathlib,re,stat,sys

ROOT=pathlib.Path(__file__).resolve().parents[2]
POLICY='HUNET_REVIEWED_P0_FROZEN_V1'
CONVERTER_SHA='e1fb5e1c276d31e064e469bc1266b572be331606886f4bdcca3c075abdd9df83'

def sha(raw):return hashlib.sha256(raw).hexdigest()
def host_path(value):
 text=str(value).replace('\\','/')
 if os.name!='nt' and re.match(r'^[A-Za-z]:/',text):text='/mnt/'+text[0].lower()+text[2:]
 return pathlib.Path(text)
def source_path_resolver(data,source_path_map,expected_sha):
 if source_path_map is None:
  if expected_sha is not None:raise ValueError('source path map and SHA required together')
  return host_path,None
 if not isinstance(expected_sha,str) or not re.fullmatch(r'[0-9a-f]{64}',expected_sha):raise ValueError('source path map and canonical SHA required together')
 path=pathlib.Path(source_path_map);before=path.lstat()
 if not stat.S_ISREG(before.st_mode) or not 0<before.st_size<=256<<10:raise ValueError('bounded regular source path map required')
 raw=path.read_bytes();after=path.lstat()
 if sha(raw)!=expected_sha or (before.st_size,before.st_mtime_ns,before.st_ino)!=(after.st_size,after.st_mtime_ns,after.st_ino):raise ValueError('source path map identity changed')
 def unique(pairs):
  result={}
  for key,value in pairs:
   if key in result:raise ValueError('duplicate source path map key')
   result[key]=value
  return result
 try:mapping=json.loads(raw,object_pairs_hook=unique)
 except (json.JSONDecodeError,UnicodeError) as error:raise ValueError('invalid source path map JSON') from error
 known={data['tensors']['path']}|{row[k]['path'] for row in data['records'] for k in ('crop','source')}
 if type(mapping) is not dict or any(type(k) is not str or type(v) is not str for k,v in mapping.items()):raise ValueError('source path map requires string keys and values')
 if set(mapping)!=known:raise ValueError('source path map requires complete known paths; missing or unknown key')
 root=path.parent.resolve(strict=True);resolved={}
 for key,value in mapping.items():
  parts=value.split('/')
  if not value or '\\' in value or ':' in value or any(ord(c)<32 for c in value) or any(part in ('','.','..') for part in parts):raise ValueError('safe relative source path map value required')
  try:
   item=root
   for part in parts:
    item=item/part
    if item.is_symlink() or getattr(item,'is_junction',lambda:False)():raise ValueError('source path map symlink/junction rejected')
   target=item.resolve(strict=True)
   if not target.is_relative_to(root) or not stat.S_ISREG(target.lstat().st_mode):raise ValueError('regular source path map target inside map parent required')
  except (OSError,RuntimeError) as error:raise ValueError('invalid source path map target') from error
  resolved[key]=target
 return lambda value:resolved[value],expected_sha

def sealed_write(path,raw):
 with path.open('xb') as stream:
  if stream.write(raw)!=len(raw):raise OSError('short fixed-input write')
  stream.flush();os.fsync(stream.fileno())
 path.chmod(0o400)

def build(roi_manifest,out,expected_manifest_sha,*,include_page_sources=False,reference_converter=None,source_path_map=None,source_path_map_sha256=None):
 source=pathlib.Path(roi_manifest);out=pathlib.Path(out)
 if out.exists() or out.is_symlink():raise FileExistsError(str(out))
 raw=source.read_bytes()
 if sha(raw)!=expected_manifest_sha:raise ValueError('original reviewed manifest identity differs')
 data=json.loads(raw)
 if data.get('coverage_status')!='USER_REVIEWED' or data.get('preprocess')!='P0_author_binary':raise ValueError('existing reviewed P0 manifest required')
 selected=[row for row in data['records'] if row.get('status')=='include' and row.get('review')=='accepted']
 if not 1<=len(selected)<=128 or len({r['id'] for r in selected})!=len(selected):raise ValueError('unique fixed crop limit128')
 pages=sorted({r['page'] for r in selected})
 if not 1<=len(pages)<=8 or any(type(n) is not int or not 1<=n<=8 for n in pages):raise ValueError('fixed page limit8')
 resolve_path,map_sha=source_path_resolver(data,source_path_map,source_path_map_sha256)
 import io,numpy as np
 converter=pathlib.Path(reference_converter) if reference_converter is not None else ROOT.parent/'hunet_original_tcu_20261008_v1/reference/roi.py'
 if sha(converter.read_bytes())!=CONVERTER_SHA:raise ValueError('frozen reviewed P0 reference changed')
 array_raw=resolve_path(data['tensors']['path']).read_bytes()
 if len(array_raw)!=data['tensors']['bytes'] or sha(array_raw)!=data['tensors']['sha256']:raise ValueError('frozen original tensor array identity changed')
 tensors=np.load(io.BytesIO(array_raw),allow_pickle=False)
 if tensors.dtype!=np.dtype('float32') or tensors.shape!=(len(data['records']),3,128,128):raise ValueError('exact original reviewed tensor array required')
 positions={row['id']:i for i,row in enumerate(data['records'])}
 # Validate every immutable source identity before preparing any new tensor.
 for row in selected:
  if not re.fullmatch(r'p[1-8]_[0-9]{3}',row['id']):raise ValueError('fixed ROI ID differs reviewed naming')
  crop=resolve_path(row['crop']['path']);s=crop.lstat()
  if not stat.S_ISREG(s.st_mode) or not 0<s.st_size<=1<<20:raise ValueError('bounded regular crop required')
  content=crop.read_bytes()
  if len(content)!=row['crop']['bytes'] or sha(content)!=row['crop']['sha256']:raise ValueError('frozen crop identity changed: '+row['id'])
 page_sources={}
 if include_page_sources:
  from PIL import Image
  for number in pages:
   rows=[r for r in selected if r['page']==number];source=rows[0]['source'];path=resolve_path(source['path']);st=path.lstat()
   if not stat.S_ISREG(st.st_mode) or not 0<st.st_size<=16<<20:raise ValueError('bounded original reviewed page source required')
   raw=path.read_bytes()
   if len(raw)!=source['bytes'] or sha(raw)!=source['sha256'] or any(r['source']['sha256']!=source['sha256'] for r in rows):raise ValueError('original reviewed page source identity differs')
   with Image.open(io.BytesIO(raw)) as image:
    w,h=image.size
    if not 0<w<=4096 or not 0<h<=4096 or w*h*3>16<<20:raise ValueError('bounded original page dimensions required')
    rgb=image.convert('RGB').tobytes();fmt=image.format
   for row in rows:
    box=row['bbox_xyxy']
    if len(box)!=4 or any(type(v) is not int for v in box) or not(0<=box[0]<box[2]<=w and 0<=box[1]<box[3]<=h):raise ValueError('reviewed box outside exact original page')
   page_sources[number]=(raw,rgb,dict(width=w,height=h,stride=w*3,source_file='page%d.source'%number,source_bytes=len(raw),source_format=fmt,rgb_file='page%d.rgb'%number,rgb_bytes=len(rgb),rgb_sha256=sha(rgb),conversion='RGB_NO_GEOMETRY_CHANGE_V1'))
 out.mkdir(parents=True,mode=0o700);payload=out/'payload';payload.mkdir(mode=0o700)
 report=dict(scope='HOST_REVIEWED_PAGE_FIXED_INPUT_PACKAGING_NO_INFERENCE',source_manifest_sha256=expected_manifest_sha,converter_sha256=CONVERTER_SHA,included=len(selected),excluded=len(data['records'])-len(selected),rows=[],natural_exit=None)
 if map_sha is not None:report['source_path_map_sha256']=map_sha
 fixed=dict(format='hunet-fixed-inputs-v2' if include_page_sources else 'hunet-fixed-inputs-v1',version=2 if include_page_sources else 1,source_manifest_sha256=expected_manifest_sha,preprocessing=POLICY,preprocessing_id=sha(POLICY.encode()),converter_sha256=CONVERTER_SHA,dtype='FP32',layout='CHW_F32',shape=[3,128,128],pages=[])
 try:
  for number in pages:
   page_rows=[r for r in selected if r['page']==number]
   original_shas={r['source']['sha256'] for r in page_rows}
   if len(original_shas)!=1:raise ValueError('page has inconsistent original image identity')
   page=dict(id='page%d'%number,title='示例图%d'%number,source_sha256=next(iter(original_shas)),items=[])
   if include_page_sources:
    raw_source,rgb,description=page_sources[number];page['image']=description;sealed_write(payload/description['source_file'],raw_source);sealed_write(payload/description['rgb_file'],rgb)
   for row in page_rows:
    input_id='example-'+row['id'];tensor=np.ascontiguousarray(tensors[positions[row['id']]],dtype='<f4').tobytes();source_bytes=resolve_path(row['crop']['path']).read_bytes()
    if sha(tensor)!=row['preprocess']['tensor_sha256']:raise ValueError('frozen reviewed tensor differs ROI pin: '+row['id'])
    if len(tensor)!=196608 or not np.isfinite(tensors[positions[row['id']]]).all():raise ValueError('finite fixed FP32 shape required')
    item=dict(input_id=input_id,roi_id=row['id'],bbox_xyxy=row['bbox_xyxy'],source_file=input_id+'.source',source_format='PNG',source_bytes=len(source_bytes),source_sha256=sha(source_bytes),tensor_file=input_id+'.f32',tensor_bytes=196608,tensor_sha256=sha(tensor))
    sealed_write(payload/item['source_file'],source_bytes);sealed_write(payload/item['tensor_file'],tensor)
    page['items'].append(item);report['rows'].append(dict(roi_id=row['id'],page=number,tensor_sha256=item['tensor_sha256'],source_sha256=item['source_sha256'],frozen_tensor_index=positions[row['id']]))
   fixed['pages'].append(page)
  encoded=(json.dumps(fixed,ensure_ascii=False,separators=(',',':'))+'\n').encode('utf8')
  if len(encoded)>65536:raise ValueError('fixed manifest exceeds64KiB')
  sealed_write(payload/'manifest.json',encoded);sealed_write(payload/'manifest.sha256',(sha(encoded)+'\n').encode())
  expected_files={'manifest.json','manifest.sha256'}|{item[key] for page in fixed['pages'] for item in page['items'] for key in ('source_file','tensor_file')}
  if include_page_sources:expected_files|={p['image'][key] for p in fixed['pages'] for key in ('source_file','rgb_file')}
  if {p.name for p in payload.iterdir()}!=expected_files:raise ValueError('unexpected fixed-input member')
  for page in fixed['pages']:
   for item in page['items']:
    for file_key,sha_key in (('source_file','source_sha256'),('tensor_file','tensor_sha256')):
     if sha((payload/item[file_key]).read_bytes())!=item[sha_key]:raise ValueError('fixed package readback drift')
  payload.chmod(0o500);report.update(natural_exit=0,manifest_bytes=len(encoded),manifest_sha256=sha(encoded),pages=[dict(id=p['id'],title=p['title'],characters=len(p['items'])) for p in fixed['pages']],files=len(expected_files),payload=str(payload),gpu_raw_cycles=None)
 except BaseException as error:
  report.update(natural_exit=1,error=repr(error));raise
 finally:
  (out/'packaging-receipt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 return report

def main(argv=None):
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--roi-manifest',required=True,type=pathlib.Path);p.add_argument('--roi-manifest-sha256',required=True);p.add_argument('--out',required=True,type=pathlib.Path);p.add_argument('--include-page-sources',action='store_true');p.add_argument('--reference-converter',type=pathlib.Path);p.add_argument('--source-path-map',type=pathlib.Path);p.add_argument('--source-path-map-sha256');a=p.parse_args(argv)
 result=build(a.roi_manifest,a.out,a.roi_manifest_sha256,include_page_sources=a.include_page_sources,reference_converter=a.reference_converter,source_path_map=a.source_path_map,source_path_map_sha256=a.source_path_map_sha256);print(json.dumps({k:v for k,v in result.items() if k!='rows'},ensure_ascii=True));return 0
if __name__=='__main__':raise SystemExit(main())
