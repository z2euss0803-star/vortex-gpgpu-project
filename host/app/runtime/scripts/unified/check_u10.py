"""U10 owner/results/RPC milestone, not complete Runtime or board acceptance."""
import hashlib,json,os,pathlib,re,struct,subprocess,sys,time
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 name,mode=sys.argv[1:];workers=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert workers in (1,2);assert mode in ('normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(name=name,mode=mode,scope='U10_NATIVE_DAEMON_AND_RELATED_HOST_PROTOCOL_OR_CROSS_NOT_KERNEL_OR_BOARD',gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=workers,test_runner_jobs=1)
 def run(argv):
  begin=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-6000:],flush=True)
  if p.returncode:raise RuntimeError(str(log)+' exit='+str(p.returncode))
  for marker in (b'ERROR: AddressSanitizer',b'WARNING: ThreadSanitizer',b'runtime error:',b'U10_ASSERT',b'U09_ASSERT',b'Assertion `'):assert marker not in p.stdout,(str(log),marker)
  return p.stdout.decode('utf8','replace')
 try:
  command=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):command+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  elfs=['test_service','rpc_fixture','test_service_legacy','test_service_alloc','test_runtime','test_compute_port','test_journal','test_journal_runtime','test_runtime_alloc','test_legacy_proof','test_legacy_port']
  targets=['test-unified-service','test-unified-rpc','test-unified-service-legacy','test-unified-service-alloc','test-unified-runtime','test-unified-runtime-bad-output','test-unified-runtime-late-cancel','test-unified-runtime-expiry','test-unified-runtime-error','test-unified-runtime-quarantine','test-unified-runtime-error-evidence','test-unified-runtime-error-log-retry','test-unified-runtime-exception','test-unified-compute-port','test-unified-compute-timeout-evidence','test-unified-compute-output-binding']
  if mode=='normal':targets+=['test-unified-wire','test-unified-identity','test-unified-scheduler','test-unified-cancel','test-unified-budget','test-unified-token-transfer','test-unified-scheduler-faults','test-unified-scheduler-alloc','test-unified-gelu-token','test-unified-cpu','test-unified-gelu','test-v2-control']
  targets+=['test-unified-compute-finish-exception']
  targets+=['test-unified-runtime-binding','test-unified-journal','test-unified-journal-runtime','test-unified-runtime-alloc','test-unified-compute-program-evidence','test-unified-legacy-proof','test-unified-legacy-port','test-unified-legacy-journal','test-unified-legacy-error-result']
  elfs+=['test_pinned_exit'];targets+=['test-unified-pinned-exit']
  elfs+=['test_service_control','test_control_alloc'];targets+=['test-unified-control','test-unified-control-alloc']
  elfs+=['test_daemon','vxgpud','test_pool_exit'];targets+=['test-unified-daemon','test-unified-daemon-fault','test-unified-daemon-legacy','test-unified-daemon-cli','test-unified-pool-exit']
  elfs+=['error_oom_probe'];targets+=['test-unified-error-oom']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']);assert run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dependency=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dependency.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];jp=pathlib.Path(lib['path']).parent.parent
   rec['json_dependency']=dict(receipt=str(dependency),receipt_sha256=sha(dependency),library=lib)
   command+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(jp)];run(command+[str(out/n) for n in elfs]+['-j'+str(workers)]);needed=set();versions=set()
   for n in elfs:
    p=out/n;raw=p.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',p]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions));rec['executed_target_elfs']=False
  else:
   prebuild=run(command+[str(out/n) for n in elfs]+['-j'+str(workers)]) if workers>1 else ''
   text=prebuild+run(command+targets+['-j1']);rec['implementation_compilations']=sum(' -c ' in line for line in text.splitlines());rec['cpu_workers']=4;rec['rpc_tests']=7
   dry=run(command+targets+['-n','-j1']);assert not re.search(r'(?:g\+\+|gcc|cc).* -(?:c|o) ',dry),'matched OUT rebuilt';rec['matched_out_recompilations']=0
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    p=pathlib.Path(folder)/n
    if n!='receipt.json':rec['artifacts'].append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
  rec['natural_exit']=0;print('U10_'+mode.upper()+'_COMPONENTS_PASS_GPU_CYCLES_NULL',flush=True)
 except Exception as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
