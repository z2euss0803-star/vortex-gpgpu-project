"""Typed lifetime and constructor-only service admission, not board execution."""
import pathlib,subprocess,json,time,sys,os,re,struct
from scripts.unified.check_u11 import artifact,sha

def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='HOST_CONSTRUCTOR_DECLARED_CONTEXT_AND_TYPED_MEMORY_ONLY_NO_GPU_WORK_NOT_BOARD',mode=mode,commands=[],artifacts=[],gpu_raw_cycles=None)
 def run(argv,expected=0,compiler=False):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));begin=time.monotonic()
  with log.open('xb') as stream:p=subprocess.run(argv,stdout=stream,stderr=subprocess.STDOUT,timeout=None if compiler else 120)
  text=log.read_text(errors='replace');rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(text[-2200:],flush=True)
  assert p.returncode==expected,'natural exit '+str(p.returncode)
  assert not any(t in text for t in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];targets=[out/'test_runtime',out/'test_native_router']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix']
   assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'))
   assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  run(cmd+targets+['-j'+str(jobs)],compiler=True)
  if mode=='red':
   assert 'U20 shared residency must count NN2 separately from media lifetime' in run([targets[0],'roles'],1)
   run(cmd+[out/'catalog-fixture.bin','-j1'],compiler=True)
   for case in ('admission','admission-declared'):assert 'U20 native admission must accept exact common roots' in run([targets[1],out/'catalog-fixture.bin',case],1)
   rec['functional_red']=['MISSING_ROLE_AWARE_NN2_LIFETIME','NATIVE_STARTUP_ROOT_ADMISSION_ENOSYS_HOST','NATIVE_STARTUP_ROOT_ADMISSION_ENOSYS_DECLARED_CONTEXT_ZERO_WORK']
  elif mode=='cross':
   needed=set();versions=set()
   for target in targets:
    raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   run(cmd+[out/'catalog-fixture.bin','-j1'],compiler=True)
   for case in ('flow','limits','stage','residency','residency-reentrant','roles'):run([targets[0],case])
   for case in ('admission','admission-declared','normal','fault','allocation','concurrency'):run([targets[1],out/'catalog-fixture.bin',case])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for p in out.rglob('*'):
   if p.is_file() and p.name!='receipt.json':rec['artifacts'].append(artifact(p))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
