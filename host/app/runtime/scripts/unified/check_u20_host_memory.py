"""Real common Runtime CPU-lifetime admission; no GPU/field execution."""
import pathlib,subprocess,json,time,sys,os,re,struct,stat,shutil
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','declared-red','normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U20_SHARED_HOST_MEMORY_RUNTIME_ONLY_NOT_GPU',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0,compiler=False):
  argv=list(map(str,argv));log=out/('command-%02d.log'%len(rec['commands']));start=time.monotonic()
  with log.open('xb') as f:
   try:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if compiler else 120)
   except subprocess.TimeoutExpired:
    rec['commands'].append(dict(argv=argv,natural_exit=None,controller_timeout_s=120,termination='OWN_DIRECT_HOST_TEST_STOPPED_NOT_GPU_DEADLINE',elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));raise
  text=log.read_text(errors='replace');rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));print(text[-2000:],flush=True)
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
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');assert p.returncode==expected;assert not any(s in text for s in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2);cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  targets=[out/'test_native_files'] if mode=='declared-red' else [out/'test_host_memory',out/'test_native_files']
  if mode not in ('red','declared-red'):targets += [out/n for n in ('test_runtime','test_runtime_alloc','test_preparation','test_pinned_exit','test_texture_source')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']);assert run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++']
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib);cmd+=['NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  run(cmd+targets+['-j'+str(jobs)],compiler=True)
  if mode=='red':
   text=run([targets[0]],1);assert 'U20 common host memory must reserve' in text
   text=run([targets[1]],1);assert 'U20 shared native output store must retain' in text
   rec['functional_red']=['COMMON_CPU_HOST_MEMORY_LIFETIME_ENOSYS','SHARED_NATIVE_OUTPUT_LIFETIME_ENOSYS']
  elif mode=='declared-red':
   text=run([targets[0],'declared-context'],1);assert 'U20 shared filesystem must admit matched declared real-IO context' in text;rec['functional_red']='FILESYSTEM_INCORRECTLY_REQUIRES_INDEPENDENT_PL_MEASUREMENT'
  elif mode=='cross':
   needed=set();versions=set()
   for target in targets:
    raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   run([targets[0]]);run([targets[1]]);run([targets[1],'declared-context'])
   for case in ('flow','limits','stage','residency','residency-reentrant'):run([out/'test_runtime',case])
   run([out/'test_runtime_alloc']);run([out/'test_preparation'])
   for case in ('result','scheduler','runtime','result-empty','scheduler-empty','runtime-empty'):run([out/'test_pinned_exit',case])
   run([out/'test_texture_source'])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for p in out.rglob('*'):
   if p.is_file() and p.name!='receipt.json':rec['artifacts'].append(artifact(p))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
