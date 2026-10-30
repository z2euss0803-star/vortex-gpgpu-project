"""U09 shared-object validation, immutable manifests are supplied by outer C gate."""
import hashlib,json,pathlib,re,struct,subprocess,sys,time,os
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='MATCHED_AARCH64_LINK_NOT_BOARD' if mode=='cross' else 'UBUNTU_SOFTWARE_NOT_BOARD',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv):
  begin=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-4500:],flush=True)
  if p.returncode:raise RuntimeError(str(log)+' exit='+str(p.returncode))
  for marker in (b'ERROR: AddressSanitizer',b'WARNING: ThreadSanitizer',b'runtime error:',b'U09_ASSERT',b'Assertion `'):assert marker not in p.stdout,(str(log),marker)
  return p.stdout.decode('utf8','replace')
 try:
  command=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];rec['debug_info']='g1; original O2/no-fast-math/fp-contract-off retained'
  if mode in ('sanitize','thread'):command+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  tests=['test_sched_scheduler','test_sched_cancel','test_sched_budget','test_sched_token_transfer','test_sched_scheduler_faults','test_sched_scheduler_alloc','test_sched_gelu_token','test_cpu_cpu','test_cpu_gelu']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];rec['compiler']=run([prefix+'g++','--version']);assert '9.2.0' in rec['compiler'];assert run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';command+=['CXX='+prefix+'g++'];targets=[str(out/t) for t in tests];run(command+targets+['-j1']);needed=set();versions=set()
   for n in tests:
    p=out/n;raw=p.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',p]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,needed=sorted(needed),required_versions=sorted(versions))
  else:
   targets=['test-unified-scheduler','test-unified-cancel','test-unified-budget','test-unified-token-transfer','test-unified-scheduler-faults','test-unified-scheduler-alloc','test-unified-gelu-token','test-unified-cpu','test-unified-gelu'];text=run(command+targets+['-j1']);compiles=[line for line in text.splitlines() if ' -c ' in line];assert len(compiles)==5,'shared implementation must compile exactly once for9 test links';rec['shared_object_compilations']=len(compiles)
   dry=run(command+targets+['-n','-j1']);assert not re.search(r'(?:g\+\+|gcc|clang\+\+).* -(?:c|o) ',dry),'matched OUT unexpectedly rebuilt';rec['matched_out_rebuild_compilations']=0
   meta=json.loads(pathlib.Path('docs/unified/U08-gelu-inputs.json').read_text());inputs=pathlib.Path(meta['target']);assert sha(inputs/'inputs-manifest.json')==meta['manifest_sha256']
   for row in meta['files']:
    p=inputs/row['path'];assert p.stat().st_size==row['bytes'] and sha(p)==row['sha256']
   replay=out/'replay';replay.mkdir();text=run([out/'test_cpu_gelu',inputs,replay]);rows=[json.loads(s) for s in text.splitlines() if s.startswith('{')];assert len(rows)==13 and sum(x['elements'] for x in rows)==1869056 and all(x['bit_diff_scalar']==0 and x['gpu_raw_cycles'] is None for x in rows);rec['replay']=rows;rec['input_manifest_sha256']=meta['manifest_sha256']
  # Source-compatible older graph declarations: compile actual U07 test, no execution claim.
  cxx=prefix+'g++' if mode=='cross' else 'g++';run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-I.','-fsyntax-only','tests/nnrt/test_graph.cpp'])
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    p=pathlib.Path(folder)/n
    if n!='receipt.json':rec['artifacts'].append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
  rec['natural_exit']=0;print('U09_'+mode.upper()+'_PASS_GPU_CYCLES_NULL',flush=True)
 except Exception as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
