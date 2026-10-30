"""Trusted B conversion receipt for one actual native Mosaic export.

This authenticates the frozen B preprocessing execution and its byte outputs.
It neither recomputes Pillow on the board nor creates inference/DP evidence.
"""
import hashlib,json,os,pathlib,stat,time
from scripts.unified.native_origin import validate_native_origin

ROOT=pathlib.Path(__file__).resolve().parents[2]
TOOLS=('scripts/unified/texture_package.py','scripts/unified/texture_pillow.py','scripts/unified/native_origin.py','vendor/p8/desktop/texture_sender.py','contracts/unified/texture-preprocess-deps.json')
def digest(raw):return hashlib.sha256(raw).hexdigest()
def wire(obj):return (json.dumps(obj,sort_keys=True,separators=(',',':'))+'\n').encode()
def identity(path):
 p=pathlib.Path(path);s=p.lstat()
 if not stat.S_ISREG(s.st_mode) or s.st_nlink!=1:raise ValueError('regular independent file required: '+str(p))
 raw=p.read_bytes();after=p.lstat()
 if (s.st_ino,s.st_size,s.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns):raise ValueError('input changed while hashing')
 return dict(bytes=len(raw),sha256=digest(raw))
def tool_identity():return {name:identity(ROOT/name) for name in TOOLS}
def verify_conversion(receipt,package,producer,result_sha,tensor_sha):
 """The expectation comes from this execution receipt, never inbox metadata."""
 if receipt.get('schema')!='s8-joint-trusted-b-conversion-v1' or receipt.get('natural_exit')!=0 or receipt.get('producer')!=producer or receipt.get('source_result_sha256')!=result_sha or receipt.get('source_tensor_sha256')!=tensor_sha or receipt.get('tools_before')!=receipt.get('tools_after') or receipt.get('gpu_raw_cycles','missing') is not None:raise ValueError('matching actual trusted B conversion execution required')
 if not isinstance(receipt.get('package_id'),str) or len(receipt['package_id'])!=64 or not receipt.get('pillow'):raise ValueError('frozen package and actual pinned Pillow receipt required')
 directory=pathlib.Path(package)
 for name,row in receipt['files'].items():
  p=directory/name;s=p.lstat()
  if s.st_uid!=os.geteuid() or stat.S_IMODE(s.st_mode)!=0o400 or identity(p)!=row:raise ValueError('converted sealed payload differs from independently recorded execution: '+name)
 manifest=json.loads((directory/'manifest.json').read_bytes())
 if manifest['package_id']!=receipt['package_id'] or manifest['source']['result_sha256']!=result_sha or manifest['source']['tensor_sha256']!=tensor_sha:raise ValueError('conversion manifest differs from frozen execution')
 if {x['name']:x['sha256'] for x in manifest['lods']}!=receipt['lod_sha256']:raise ValueError('actual conversion LODs differ')
 return receipt['package_id']
def convert(source,out,producer):
 from scripts.unified.texture_package import build_package
 from scripts.unified.texture_pillow import dependency
 source=pathlib.Path(source);out=pathlib.Path(out);out.mkdir(mode=0o700,parents=True,exist_ok=False)
 source_identity=identity(source);meta=json.loads(source.read_bytes());origin=validate_native_origin(meta)
 if not origin or origin['provider_flags']!=4 or {k:origin[k] for k in ('boot_id','client_id','request_id')}!=producer:raise ValueError('current original joint Mosaic producer required')
 artifact=next(x for x in meta['artifacts'] if x['role']=='output');tensor_identity=identity(source.parent/artifact['name'])
 if tensor_identity['sha256']!=artifact['sha256']:raise ValueError('actual exported tensor digest differs')
 before=tool_identity();pillow=dependency();begin=time.monotonic_ns() if hasattr(time,'monotonic_ns') else int(time.monotonic()*1e9)
 package=out/'package';result=build_package(source,package);end=time.monotonic_ns() if hasattr(time,'monotonic_ns') else int(time.monotonic()*1e9)
 if result['source']['result_sha256']!=source_identity['sha256'] or result['source']['tensor_sha256']!=tensor_identity['sha256']:raise ValueError('conversion must consume the original export')
 files={}
 for p in sorted(package.rglob('*')):
  if p.is_symlink():raise ValueError('conversion cannot publish a link')
  if p.is_file():files[p.relative_to(package).as_posix()]=identity(p);p.chmod(0o400)
  elif p.is_dir():p.chmod(0o700)
 after=tool_identity()
 if before!=after or identity(source)!=source_identity or identity(source.parent/artifact['name'])!=tensor_identity:raise ValueError('frozen source/converter changed during preprocessing')
 receipt=dict(schema='s8-joint-trusted-b-conversion-v1',scope='TRUSTED_B_HOST_PREPROCESS_NOT_GPU_OR_DP',natural_exit=0,producer=producer,source_scope=meta['scope'],source_numeric_class=meta['numeric_class'],source_result_sha256=source_identity['sha256'],source_tensor_sha256=tensor_identity['sha256'],package_id=result['package_id'],lod_sha256={x['name']:x['sha256'] for x in result['lods']},files=files,pillow=pillow,tools_before=before,tools_after=after,conversion_ns=end-begin,gpu_raw_cycles=None)
 verify_conversion(receipt,package,producer,source_identity['sha256'],tensor_identity['sha256'])
 path=out/'conversion-receipt.json'
 with path.open('xb') as f:f.write(wire(receipt));f.flush();os.fsync(f.fileno())
 path.chmod(0o400)
 return receipt,package
