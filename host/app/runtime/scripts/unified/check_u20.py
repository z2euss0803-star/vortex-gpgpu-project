"""U20 staged native integration validation; prerequisites never close full U20."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('prepare-red','prepare-normal','residency-red','residency-normal','residency-lock-red','residency-sanitize','residency-thread','residency-cross','stage-red','stage-normal','stage-display-red','stage-display-normal','runtime-stage-red','runtime-stage-normal','stage-cookie-red','stage-roots-red','native-red','native-normal')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 rec=dict(scope='U20_NATIVE_INTEGRATION_PREREQUISITES_ONLY_NOT_APPS_GPU_OR_FULL_TASK',mode=mode,commands=[],artifacts=[],gpu_raw_cycles=None,compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0):
  argv=list(map(str,argv));start=time.monotonic();log=out/('command-%02d.log'%len(rec['commands']));limit=30 if argv[0]==str(out/'test_native_router') else None
  try:p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=limit)
  except subprocess.TimeoutExpired as e:
   log.write_bytes(e.output or b'');rec['commands'].append(dict(argv=argv,natural_exit=None,controller_timeout_s=limit,termination='PYTHON_RUN_KILLS_OWN_DIRECT_UNIT_CHILD',elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));raise
  log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-5000:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode)
  assert not any(s in text for s in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode.startswith('native-'):
   run(cmd+[out/'test_native_router',out/'catalog-fixture.bin','-j'+str(jobs)])
   if mode=='native-red':
    text=run([out/'test_native_router',out/'catalog-fixture.bin','normal'],1);assert 'U20 trusted native request router must share common service roots' in text;rec['functional_red']='COMMON_NATIVE_SOURCE_DISPATCH_AND_SHARED_LIFETIME_ROUTER_MISSING'
   else:
    run([out/'test_native_router',out/'catalog-fixture.bin','normal']);run([out/'test_native_router',out/'catalog-fixture.bin','fault'])
   rec['natural_exit']=0;return
  if mode=='stage-roots-red':
   run(cmd+[out/'test_journal',out/'test_daemon',out/'catalog-fixture.bin','-j'+str(jobs)])
   text=run([out/'test_journal',out/'journal-stage','stage'],1);assert 'U20 private display staging needs a distinct durable journal record' in text
   text=run([out/'test_daemon',out/'catalog-fixture.bin',out/'daemon-roots-journal','deferred'],1);assert 'U20 native builder must share actual Runtime result store and journal roots' in text;rec['functional_red']='DISTINCT_DURABLE_STAGE_JOURNAL_AND_NATIVE_SHARED_WEAK_ROOTS_MISSING';rec['natural_exit']=0;return
  if mode.startswith('runtime-stage-'):
   run(cmd+[out/'test_runtime','-j'+str(jobs)])
   if mode=='runtime-stage-red':
    text=run([out/'test_runtime','stage'],1);assert 'U20 native output staging must bind the active final owner callback' in text;rec['functional_red']='NATIVE_DISPLAY_STAGE_NOT_BOUND_TO_FINAL_ACTIVE_RUNTIME_OWNER_CALLBACK'
   else:run(cmd+['test-unified-runtime-stage','test-unified-runtime','test-unified-runtime-residency','test-unified-runtime-binding','test-unified-runtime-alloc','test-unified-runtime-bad-output','test-unified-runtime-late-cancel','test-unified-runtime-expiry','test-unified-runtime-error','test-unified-runtime-quarantine','test-unified-runtime-exception','test-unified-runtime-error-evidence','test-unified-runtime-error-log-retry','-j1'])
   rec['natural_exit']=0;return
  if mode.startswith('stage-display-'):
   run(cmd+[out/'test_stage_display','-j'+str(jobs)])
   if mode=='stage-display-red':
    text=run([out/'test_stage_display'],1);assert 'U20 common display owner must read retained private stage' in text
    text=run([out/'test_stage_display','cookie'],1);assert 'U20 native application cookie must bind to actual common DP source hold' in text;rec['functional_red']='DISPLAY_READS_ONLY_PUBLIC_RESULTS_AND_HAS_NO_APP_COOKIE_BINDING'
   else:
    run(cmd+['test-unified-stage-display','test-unified-display','test-unified-page-switch','test-unified-result-stage','test-unified-service','test-unified-service-alloc','-j1'])
   rec['natural_exit']=0;return
  if mode.startswith('stage-'):
   run(cmd+[out/'test_result_stage','-j'+str(jobs)])
   if mode=='stage-cookie-red':
    text=run([out/'test_result_stage'],1);assert 'U20 automatic DP cookie must not collide with active application cookie' in text;rec['functional_red']='AUTO_DP_TOKEN_COLLIDES_WITH_LIVE_LOGICAL_APP_COOKIE'
   elif mode=='stage-red':
    text=run([out/'test_result_stage'],1);assert 'U20 checked output needs private pre-display staging before public completion' in text;rec['functional_red']='CHECKED_PRE_DISPLAY_SOURCE_NOT_AVAILABLE_WITHOUT_EARLY_PUBLIC_COMPLETION'
   else:run(cmd+['test-unified-result-stage','test-unified-service','test-unified-service-alloc','-j1'])
   rec['natural_exit']=0;return
  if mode in ('residency-sanitize','residency-thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='residency-sanitize' else 'thread')]
  if mode=='residency-cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  if mode.startswith('residency-'):
   run(cmd+[out/'test_runtime','-j'+str(jobs)])
   if mode=='residency-lock-red':
    text=run([out/'test_runtime','residency-reentrant'],-14);assert 'U20_ENTER_REENTRANT_RESIDENT_RETIRE' in text;rec['functional_red']='RESIDENT_DESTRUCTOR_REENTERS_REQUEST_RETIREMENT_MUTEX_BOUNDED_SIGALRM'
   elif mode=='residency-red':
    text=run([out/'test_runtime','residency'],1);assert 'U20 resident lifetime must be charged before native session allocation' in text;rec['functional_red']='RESIDENT_SHADOW_LIFETIME_NOT_GLOBALLY_ACCOUNTED'
   elif mode=='residency-cross':
    elfs=[out/'test_runtime',out/'test_runtime_alloc',out/'test_daemon'];run(cmd+elfs+['-j'+str(jobs)])
    needed=set();versions=set()
    for elf in elfs:
     raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
     text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
    sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
    runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
   else:
    run(cmd+[out/'test_runtime_alloc',out/'test_daemon',out/'catalog-fixture.bin','-j'+str(jobs)])
    run(cmd+['test-unified-runtime','test-unified-runtime-residency','test-unified-runtime-binding','test-unified-runtime-alloc','test-unified-runtime-bad-output','test-unified-runtime-late-cancel','test-unified-runtime-expiry','test-unified-runtime-error','test-unified-runtime-quarantine','test-unified-runtime-exception','test-unified-runtime-error-evidence','test-unified-runtime-error-log-retry','test-unified-daemon','test-unified-daemon-fault','test-unified-daemon-legacy','test-unified-daemon-deferred','-j1'])
   rec['natural_exit']=0;return
  run(cmd+[out/'test_daemon',out/'catalog-fixture.bin','-j'+str(jobs)])
  if mode=='prepare-red':
   text=run([out/'test_daemon',out/'catalog-fixture.bin',out/'daemon-deferred-journal','deferred'],1);assert 'U20 deferred native prepare hook must reach actual owner port' in text;rec['functional_red']='DAEMON_OMITS_NATIVE_DEFERRED_PREPARE_HOOK_FORWARDING'
  else:run(cmd+['test-unified-daemon','test-unified-daemon-fault','test-unified-daemon-legacy','test-unified-daemon-deferred','-j1'])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
