"""Adversarial real filesystem packages; no GPU, DP or device operation."""
import copy,hashlib,importlib.util,json,os,pathlib,shutil,struct,subprocess,sys,time,zlib
from scripts.unified.texture_package import build_package
from scripts.unified.texture_pillow import dependency

def main():
 name,mode=sys.argv[1:];assert mode in ('order-probe','normal','sanitize');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='ACTUAL_CPP_PACKAGE_ADMISSION_AND_FILESYSTEM_MUTATIONS_NOT_GPU_DP',gpu_raw_cycles=None,commands=[],cases=[])
 def run(argv,expected=0):
  begin=time.monotonic();p=subprocess.run(list(map(str,argv)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%02d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=list(map(str,argv)),natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=hashlib.sha256(p.stdout).hexdigest()));print(p.stdout.decode('utf8','replace')[-1500:],flush=True);assert p.returncode==expected;assert not any(x in p.stdout for x in (b'ERROR: AddressSanitizer',b'runtime error:'));return p.stdout
 def rewrite(path,data):
  # All cases share hardlinked original payloads until this explicit new inode.
  path=pathlib.Path(path);temporary=path.with_name(path.name+'.mutated');temporary.write_bytes(data);os.replace(str(temporary),str(path))
 def manifest(path,m):
  m=copy.deepcopy(m);m.pop('package_id',None);m['package_id']=hashlib.sha256(json.dumps(m,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()).hexdigest();rewrite(path/'manifest.json',(json.dumps(m,sort_keys=True)+'\n').encode());return m['package_id']
 try:
  rec['dependency']=dependency();spec=importlib.util.spec_from_file_location('fixture','tests/unified/test_texture_package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.Package().fixture(out/'input');base=out/'base';metadata=build_package(out/'input/result.json',base);elf=out/'test_texture_admission';cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode=='sanitize':cmd+=['SANITIZE=address,undefined']
  run(cmd+[elf,'-j'+os.environ.get('VX_UNIFIED_JOBS','1')]);run([elf,base,metadata['package_id'],'valid'])
  ordered=out/'reordered-input';module.Package().fixture(ordered);original=json.loads((ordered/'result.json').read_bytes());original['identities']=dict(reversed(list(original['identities'].items())));rewrite(ordered/'result.json',(json.dumps(original,sort_keys=False)+'\n').encode());package=out/'reordered-package';order_meta=build_package(ordered/'result.json',package)
  log=run([elf,package,order_meta['package_id'],'valid'])
  if mode=='order-probe':rec.update(order_probe='ORDER_ALREADY_CANONICALIZED_NO_PRODUCT_CHANGE',natural_exit=0);return
  names=('metadata-drift','duplicate-key','unknown-field','scope-upgrade','source-kind','original-cycles','claimed-gpu-cycles','claimed-gpu-acceptance','claimed-visual','preprocessor-drift','pillow-drift','escape','nul-path','source-parent-symlink','source-leaf-symlink','missing-manifest','missing-lod','fifo-lod','truncated-lod','wrong-sha','wrong-crc','rehashed-alpha','wrong-lod-shape','extra-lod','rehashed-rgb','trailing-original-tensor')
  for kind in names:
   target=out/('case-'+kind);shutil.copytree(str(base),str(target),copy_function=os.link);m=copy.deepcopy(metadata);expected_id=m['package_id']
   if kind=='metadata-drift':m['source']['generation']+=1;rewrite(target/'manifest.json',json.dumps(m).encode())
   elif kind=='duplicate-key':raw=(target/'manifest.json').read_bytes().rstrip();rewrite(target/'manifest.json',raw[:-1]+b',"schema":"vortex-result-texture-v1"}')
   elif kind=='unknown-field':m['unbound']=1
   elif kind=='scope-upgrade':m['source']['scope']='REAL_BOARD'
   elif kind=='source-kind':m['source']['kind']='PHOTO_UPLOAD'
   elif kind=='original-cycles':m['source']['original_cycle_valid']=True;m['source']['original_raw_cycles']=1
   elif kind=='claimed-gpu-cycles':m['gpu_raw_cycles']=1
   elif kind=='claimed-gpu-acceptance':m['gpu_execution_acceptance']=True
   elif kind=='claimed-visual':m['visual_acceptance']=True
   elif kind=='preprocessor-drift':m['preprocessing']['vendor_sha256']='b'*64
   elif kind=='pillow-drift':m['preprocessing']['pillow_wheel_sha256']='b'*64
   elif kind=='escape':m['rgb']['name']='../base/derived/rgb8.bin'
   elif kind=='nul-path':m['rgb']['name']='derived/evil\0rgb'
   elif kind=='source-parent-symlink':os.rename(str(target/'source'),str(target/'source-original'));os.symlink(str((base/'source').resolve()),str(target/'source'))
   elif kind=='source-leaf-symlink':p=target/m['source']['result_json'];p.unlink();os.symlink(str((base/m['source']['result_json']).resolve()),str(p))
   elif kind=='missing-manifest':(target/'manifest.json').unlink()
   elif kind=='missing-lod':(target/m['lods'][1]['name']).unlink()
   elif kind=='fifo-lod':p=target/m['lods'][0]['name'];p.unlink();os.mkfifo(str(p))
   elif kind=='truncated-lod':p=target/m['lods'][1]['name'];rewrite(p,p.read_bytes()[:-4])
   elif kind=='wrong-sha':m['lods'][1]['sha256']='b'*64
   elif kind=='wrong-crc':m['lods'][1]['crc32']^=1
   elif kind=='rehashed-alpha':row=m['lods'][0];p=target/row['name'];data=bytearray(p.read_bytes());data[3]=0;rewrite(p,data);row['sha256']=hashlib.sha256(data).hexdigest();row['crc32']=zlib.crc32(data)
   elif kind=='wrong-lod-shape':m['lods'][0]['width']=511
   elif kind=='extra-lod':m['lods'].append(copy.deepcopy(m['lods'][0]))
   elif kind=='rehashed-rgb':p=target/m['rgb']['name'];data=bytearray(p.read_bytes());data[0]^=1;rewrite(p,data);m['rgb']['sha256']=hashlib.sha256(data).hexdigest()
   elif kind=='trailing-original-tensor':p=target/m['source']['tensor'];rewrite(p,p.read_bytes()+b'\0')
   if kind not in ('metadata-drift','duplicate-key','missing-manifest'):expected_id=manifest(target,m)
   run([elf,target,expected_id,'reject']);rec['cases'].append(kind)
  assert len(rec['cases'])==26;rec.update(natural_exit=0,reordered_identity_positive=True);print('U17_ADVERSARIAL_26_REFUSALS_AND_UNORDERED_IDENTITY_PASS_HOST_NOT_GPU_DP',flush=True)
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()
