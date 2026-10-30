"""Common native routing validation, not application inference or GPU evidence."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha

def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U20_NATIVE_ROUTER_ACTUAL_COMMON_C_CONTROL_NOT_APPLICATIONS_OR_GPU',mode=mode,commands=[],artifacts=[],gpu_raw_cycles=None,compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,limit=None):
  argv=list(map(str,argv));begin=time.monotonic();log=out/('command-%03d.log'%len(rec['commands']))
  try:p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=limit)
  except subprocess.TimeoutExpired as e:
   log.write_bytes(e.output or b'');rec['commands'].append(dict(argv=argv,natural_exit=None,controller_timeout_s=limit,termination='PYTHON_RUN_KILLS_OWN_DIRECT_TEST_CHILD',log=str(log),sha256=sha(log)));raise
  log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-3000:],flush=True)
  assert p.returncode==0,'natural exit '+str(p.returncode)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  elfs=[out/n for n in ('test_native_router','test_runtime','test_runtime_alloc','test_daemon')];run(cmd+elfs+[out/'catalog-fixture.bin','-j'+str(jobs)])
  for elf in elfs:
   raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for elf in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   for case in ('normal','fault','allocation','concurrency'):run([out/'test_native_router',out/'catalog-fixture.bin',case],30)
   for case in ('flow','limits','cancel','unknown','offline-cycles','stage','residency','residency-reentrant','binding','bad-output','late-cancel','expiry','error','exception','error-evidence','error-log-retry'):run([out/'test_runtime',case],30)
   run([out/'test_runtime_alloc'],60)
   for case in ('normal','fault','legacy','deferred'):run([out/'test_daemon',out/'catalog-fixture.bin',out/('daemon-'+case+'-journal')]+([] if case=='normal' else [case]),60)
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
