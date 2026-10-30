"""Mosaic result -> frozen P8 dual LOD. Host files only, never GPU/DP proof."""
import array,hashlib,importlib.util,json,math,os,pathlib,stat,sys,zlib
from demo.contracts import validate_run,relative_name

def sha(b):return hashlib.sha256(b).hexdigest()
def pairs(rows):
 result={}
 for key,value in rows:
  if key in result:raise ValueError('duplicate JSON key')
  result[key]=value
 return result
def invalid(value):raise ValueError('non-integer/nonfinite JSON metadata')
def safe_read(fd,maximum,exact=None):
 before=os.fstat(fd)
 if not stat.S_ISREG(before.st_mode) or not 0<before.st_size<=maximum or (exact is not None and before.st_size!=exact):raise ValueError('bounded regular file extent')
 blocks=[];left=before.st_size
 while left:
  block=os.read(fd,min(left,1<<20))
  if not block:raise ValueError('short file read')
  blocks.append(block);left-=len(block)
 after=os.fstat(fd)
 if os.read(fd,1) or (after.st_size,after.st_mtime_ns)!=(before.st_size,before.st_mtime_ns):raise ValueError('file changed while read')
 return b''.join(blocks)
def child_read(root,name,maximum,exact=None):
 relative_name(name);fd=os.dup(root)
 try:
  parts=name.split('/')
  for i,part in enumerate(parts):
   next_fd=os.open(part,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK|(os.O_DIRECTORY if i+1<len(parts) else 0),dir_fd=fd);os.close(fd);fd=next_fd
  return safe_read(fd,maximum,exact)
 finally:os.close(fd)
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 with path.open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
def build_package(result_json_path,destination):
 source=pathlib.Path(result_json_path);out=pathlib.Path(destination)
 if out.exists():raise FileExistsError('new package directory required')
 if sys.byteorder!='little':raise ValueError('FP32_LE host required')
 root=os.open(str(source.parent),os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
 try:
  text=child_read(root,source.name,256*1024);meta=json.loads(text.decode('utf8'),object_pairs_hook=pairs,parse_float=invalid,parse_constant=invalid);validate_run(meta)
  from scripts.unified.native_origin import validate_native_origin
  validate_native_origin(meta)
  if meta['app']!='mosaic' or meta['state']!='RESULT_VALIDATED' or meta['numeric_class']=='FAIL' or meta['scope']=='MOCK':raise ValueError('complete traceable Mosaic result required')
  if meta['scope']=='REAL_BOARD' and (not meta['cycle_valid'] or not meta['raw_cycles']):raise ValueError('missing original physical cycles')
  shape=meta['shape']
  if len(shape)!=4 or shape[:2]!=[1,3] or not 0<shape[2]<=1080 or not 0<shape[3]<=1920:raise ValueError('bounded CHW shape')
  outputs=[x for x in meta['artifacts'] if x['role']=='output']
  if len(outputs)!=1:raise ValueError('one output required')
  artifact=outputs[0];h,w=shape[2:];pixels=h*w
  if artifact['dtype']!='FP32' or artifact['shape']!=shape or artifact['bytes']!=pixels*12 or artifact['name']=='__mosaic_result.json':raise ValueError('output tensor extent/layout')
  raw=child_read(root,artifact['name'],64*1024*1024,artifact['bytes'])
  if sha(raw)!=artifact['sha256']:raise ValueError('output SHA drift')
 finally:os.close(root)
 values=array.array('f');values.frombytes(raw)
 if values.itemsize!=4 or any(not math.isfinite(v) for v in values):raise ValueError('nonfinite FP32 output')
 rgb=bytearray(pixels*3)
 for c in range(3):
  for i in range(pixels):
   v=values[c*pixels+i];rgb[3*i+c]=0 if v<=0 else 255 if v>=255 else int(v)
 from scripts.unified.texture_pillow import dependency
 pinned_dependency=dependency()
 from PIL import Image,__version__ as pillow_version
 image=Image.frombytes('RGB',(w,h),bytes(rgb));vendor=pathlib.Path(__file__).resolve().parents[2]/'vendor/p8/desktop/texture_sender.py';specs=json.loads((pathlib.Path(__file__).resolve().parents[2]/'docs/unified/U17-inputs.json').read_bytes());expected=next(x['sha256'] for x in specs['files'] if x['candidate']=='vendor/p8/desktop/texture_sender.py')
 if sha(vendor.read_bytes())!=expected:raise ValueError('frozen preprocessor identity drift')
 spec=importlib.util.spec_from_file_location('frozen_p8_texture_sender',str(vendor));sender=importlib.util.module_from_spec(spec);spec.loader.exec_module(sender);lods=[sender.prepare_lod(image,size).tobytes() for size in (512,2048)]
 if tuple(map(len,lods))!=(1048576,16777216):raise ValueError('dualLOD extent')
 source_json='source/__mosaic_result.json';tensor='source/'+artifact['name'];rgb_name='derived/rgb8.bin';rows=[dict(name='lod%d.rgba'%size,width=size,height=size,format='RGBA8',bytes=len(data),sha256=sha(data),crc32=zlib.crc32(data)) for size,data in zip((512,2048),lods)]
 result=dict(schema='vortex-result-texture-v1',source=dict(kind='MOSAIC_RESULT',scope=meta['scope'],run_id=meta['run_id'],boot_id=meta['boot_id'],generation=meta['generation'],identities=meta['identities'],numeric_class=meta['numeric_class'],result_json=source_json,result_sha256=sha(text),tensor=tensor,tensor_sha256=sha(raw),shape=shape,bytes=len(raw),original_cycle_valid=meta['cycle_valid'],original_raw_cycles=meta['raw_cycles']),rgb=dict(name=rgb_name,layout='HWC_RGB8',shape=[h,w,3],bytes=len(rgb),sha256=sha(rgb)),preprocessing=dict(policy='cover-visible-mean-v1',geometry='cover',crop='center',resampling='LANCZOS',exif_orientation=1,source='NN_CHW_FP32_NO_EXIF',gamma_or_enhancement=False,rgba_opaque=True,vendor_sha256=expected,pillow_version=pillow_version,pillow_wheel_sha256=pinned_dependency['wheel_sha256']),lods=rows,gpu_raw_cycles=None,gpu_execution_acceptance=False,visual_acceptance=False)
 canonical=json.dumps(result,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode();result['package_id']=sha(canonical);out.mkdir(exist_ok=False);write(out/source_json,text);write(out/tensor,raw);write(out/rgb_name,bytes(rgb))
 for row,data in zip(rows,lods):write(out/row['name'],data)
 # File fsync alone does not make the child directory links durable. Flush
 # every newly created payload ancestor, then its parent, before the manifest
 # becomes the public completion marker.
 if os.name=='posix':
  directories={out,out.parent}
  for name in [source_json,tensor,rgb_name]+[row['name'] for row in rows]:
   parent=(out/name).parent
   while parent!=out.parent:directories.add(parent);parent=parent.parent
  for directory in sorted(directories,key=lambda x:len(x.parts),reverse=True):
   fd=os.open(str(directory),os.O_RDONLY|os.O_DIRECTORY)
   try:os.fsync(fd)
   finally:os.close(fd)
 write(out/'manifest.json.tmp',(json.dumps(result,sort_keys=True,indent=2)+'\n').encode());os.replace(str(out/'manifest.json.tmp'),str(out/'manifest.json'))
 if os.name=='posix':
  fd=os.open(str(out),os.O_RDONLY|os.O_DIRECTORY)
  try:os.fsync(fd)
  finally:os.close(fd)
 return result
if __name__=='__main__':
 import argparse
 parser=argparse.ArgumentParser();parser.add_argument('result_json');parser.add_argument('destination');args=parser.parse_args()
 if sys.version_info[:2]==(3,6):
  from scripts.unified.texture_pillow import dependency
  dependency()
 print(json.dumps(build_package(args.result_json,args.destination),indent=2))
