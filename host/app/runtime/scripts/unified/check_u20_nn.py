from scripts.unified.hunet_program import selected_directory as selected_hunet_directory
"""Real native NN factory/common owner integration; host marker is not inference."""
import json,os,pathlib,struct,subprocess,sys,time,re,stat,shutil
from scripts.unified.mosaic_program import selected_directory
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','storage-red','normal','sanitize','thread','cross')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U20_REAL_NATIVE_NN_FACTORY_CONTROL_AND_OUTPUT_FILES_HOST_NOT_GPU_INFERENCE',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0,limit=None):
  argv=list(map(str,argv));begin=time.monotonic();log=out/('command-%03d.log'%len(rec['commands']))
  try:p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=limit)
  except subprocess.TimeoutExpired as e:
   log.write_bytes(e.output or b'');rec['commands'].append(dict(argv=argv,natural_exit=None,controller_timeout_s=limit,termination='PYTHON_RUN_KILLS_OWN_DIRECT_TEST_CHILD',log=str(log),sha256=sha(log)));raise
  log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));text=p.stdout.decode('utf8','replace');print(text[-3500:],flush=True)
  for owner in re.findall(r'^NATIVE_FILES_OWNER=(/tmp/vx-native-files-[A-Za-z0-9]{6})$',text,re.M):
   source=pathlib.Path(owner);state=source.lstat();assert state.st_uid==os.geteuid() and stat.S_ISDIR(state.st_mode) and stat.S_IMODE(state.st_mode)==0o700;dest=out/('native-private-%03d'%len(rec['commands']));dest.mkdir();members=[]
   for folder,dirs,files in os.walk(str(source),followlinks=False):
    for name in sorted(dirs+files):
     path=pathlib.Path(folder)/name;s=path.lstat();assert s.st_uid==os.geteuid();target=dest/path.relative_to(source);entry=dict(path=str(path.relative_to(source)),mode=stat.S_IMODE(s.st_mode),original_links=s.st_nlink)
     if stat.S_ISDIR(s.st_mode):target.mkdir(exist_ok=False);entry['kind']='directory'
     elif stat.S_ISREG(s.st_mode):
      shutil.copyfile(str(path),str(target));assert sha(path)==sha(target);after=path.lstat();assert (after.st_ino,after.st_size,after.st_mtime_ns)==(s.st_ino,s.st_size,s.st_mtime_ns);target.chmod(stat.S_IMODE(s.st_mode));entry.update(kind='file',bytes=s.st_size,sha256=sha(target))
     elif stat.S_ISLNK(s.st_mode):entry.update(kind='symlink_metadata_only_no_follow',target=os.readlink(str(path)))
     else:entry['kind']='nonfile_metadata_only'
     members.append(entry)
   rec.setdefault('native_private_evidence',[]).append(dict(original_owner=owner,copy=str(dest),members=members))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text())
  from scripts.unified.generate_mosaic_package import generate
  weights=pathlib.Path(env['mosaic_assets']);input_path=out/'input-small.f32';input_path.write_bytes(b''.join(struct.pack('<f',(i%37-18)*.025) for i in range(288)));model=out/'model-small';rec['model']=generate(weights,weights.parent/'manifest.json',input_path,12,8,64,model)
  for f in model.iterdir():f.chmod(0o400)
  model.chmod(0o500)
  hunet=out/'hunet-model'
  if mode not in ('red','storage-red'):
   weights=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin');assert sha(weights)=='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc'
   rec['hunet_model']=json.loads(run([sys.executable,'-m','scripts.unified.generate_hunet_package',weights,hunet]).strip())
   for f in hunet.iterdir():f.chmod(0o400)
   hunet.chmod(0o500)
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_native_nn'
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib);cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  elfs=[elf] if mode in ('red','storage-red') else [elf,out/'test_native_nn_hunet',out/'test_native_router']+[out/n for n in ('test_host_memory','test_native_files','test_runtime','test_runtime_alloc','test_preparation','test_pinned_exit','test_texture_source')];build_log=run(cmd+elfs+['-j'+str(jobs)])
  if mode not in ('red','storage-red'):
   counts={path:sum((' -c '+path+' -o ') in line for line in build_log.splitlines()) for path in ('unified/service/native.cpp','unified/service/native_files.cpp','unified/service/compute_port.cpp')};assert all(n==1 for n in counts.values())
   rec['object_reuse']=dict(scope='IDENTICAL_SOURCE_TOOL_FLAGS_WITHIN_ONE_MAKE_OUT_ONLY_NOT_RESULT_REUSE',compile_counts=counts,sanitizer_out_separate=True)
  for target in elfs:
   raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for target in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   catalog=out/'mosaic-owner-catalog.bin';run(cmd+[catalog,'-j1'])
   for case in (('flow',) if mode=='red' else ('shared-files',) if mode=='storage-red' else ('flow','guard','files','lifetime','shared-files')):
    text=run([elf,model,catalog,selected_directory('.')/'code.bin',selected_directory('.')/'rodata.bin',out/('outputs-'+case),case],1 if mode in ('red','storage-red') else 0,90)
    if mode=='red':assert 'U20 actual native NN factory must use common model lifetime and immutable files' in text;rec['functional_red']='REAL_NATIVE_MODEL_FACTORY_AND_CHECKED_OUTPUT_FILES_MISSING'
    if mode=='storage-red':assert 'U20 NN factory must reuse sole common NativeFiles budget' in text;rec['functional_red']='NN_COMMON_OUTPUT_REPOSITORY_NOT_BOUND'
   if mode not in ('red','storage-red'):
    catalog=out/'hunet-owner-catalog.bin';run(cmd+[catalog,out/'catalog-fixture.bin','-j1'])
    for case in ('flow','guard','files','lifetime','shared-files'):run([out/'test_native_nn_hunet',hunet,catalog,selected_hunet_directory('.')/'code.bin','unused',out/('hunet-outputs-'+case),case],limit=90)
    for case in ('normal','fault','allocation','concurrency','admission','admission-declared'):run([out/'test_native_router',out/'catalog-fixture.bin',case],limit=60)
    for case in ('flow','lifetime','shared-files'):
     files=list((out/('hunet-outputs-'+case)).glob('*/trace.raw'));assert len(files)==1;rows=[json.loads(line) for line in files[0].read_text().splitlines()];assert len(rows)==116 and all(r['gpu_raw_cycles'] is None for r in rows);cpu=[r for r in rows if r['cpu']];assert len(cpu)==13 and all(r['gelu']['committed'] and r['gelu']['verified'] and r['gelu']['pool']['workers']==4 and r['gelu']['output_sha']==r['gelu']['readback_sha'] for r in cpu);rec.setdefault('hunet_original_trace_checks',[]).append(dict(case=case,nodes=len(rows),gpu=103,cpu=13,scope='HOST_CONTROL_AND_REAL_CPU_POOL_NOT_GPU_MATH'))
    run([out/'test_host_memory']);run([out/'test_native_files']);run([out/'test_native_files','declared-context'])
    for case in ('flow','limits','stage','residency','residency-reentrant','roles'):run([out/'test_runtime',case])
    run([out/'test_runtime_alloc']);run([out/'test_preparation'])
    for case in ('result','scheduler','runtime','result-empty','scheduler-empty','runtime-empty'):run([out/'test_pinned_exit',case])
    run([out/'test_texture_source']);rec['common_regressions']='SAME_MAKE_OUT_SHARED_OBJECTS_SOURCE_FLAGS_NO_TEST_RESULT_REUSE_HOST_MEMORY_FILES_DECLARED_CONTEXT_RUNTIME_OOM_PIN_PREPARATION_IMPORT'
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
