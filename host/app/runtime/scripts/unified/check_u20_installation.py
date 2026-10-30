from scripts.unified.hunet_program import selected_directory as selected_hunet_directory
"""Sealed NN installation through real native Daemon RPC, still host markers."""
import json,os,pathlib,struct,subprocess,sys,time,re,stat,shutil
from scripts.unified.mosaic_program import selected_directory
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread','cross')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U20_SEALED_NN_INSTALLATION_ACTUAL_DAEMON_RPC_HOST_CONTROL_NOT_GPU',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0,limit=240):
  argv=list(map(str,argv));begin=time.monotonic();log=out/('command-%03d.log'%len(rec['commands']));deadline=None if argv[0]=='make' else limit
  # Compiler duration is not a GPU/request deadline. Keep the actual make
  # process and all output alive under coordination until natural completion.
  with log.open('xb') as stream:
   try:p=subprocess.run(argv,stdout=stream,stderr=subprocess.STDOUT,timeout=deadline)
   except subprocess.TimeoutExpired:
    rec['commands'].append(dict(argv=argv,natural_exit=None,elapsed_s=time.monotonic()-begin,controller_timeout_s=deadline,termination='SUBPROCESS_RUN_STOPS_OWN_DIRECT_HOST_TEST_CHILD',log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(log.read_text(errors='replace')[-2200:],flush=True);raise
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,controller_timeout_s=deadline,log=str(log),sha256=sha(log)));text=log.read_text(errors='replace');print(text[-2200:],flush=True)
  owners=re.findall(r'^NATIVE_DAEMON_OWNER=(/tmp/vx-native-daemon-[A-Za-z0-9]{6})$',text,re.M)
  for owner in owners:
   source=pathlib.Path(owner);s=source.lstat();assert s.st_uid==os.geteuid() and stat.S_ISDIR(s.st_mode) and stat.S_IMODE(s.st_mode)==0o700;dest=out/('native-private-%03d'%len(rec['commands']));dest.mkdir();members=[]
   for folder,dirs,files in os.walk(str(source),followlinks=False):
    for name in sorted(dirs+files):
     path=pathlib.Path(folder)/name;state=path.lstat();assert state.st_uid==os.geteuid();target=dest/path.relative_to(source);entry=dict(path=str(path.relative_to(source)),mode=stat.S_IMODE(state.st_mode),original_links=state.st_nlink)
     if stat.S_ISDIR(state.st_mode):target.mkdir(exist_ok=False);entry['kind']='directory'
     elif stat.S_ISREG(state.st_mode):
      shutil.copyfile(str(path),str(target));assert sha(path)==sha(target);after=path.lstat();assert (after.st_ino,after.st_mtime_ns,after.st_size)==(state.st_ino,state.st_mtime_ns,state.st_size);target.chmod(stat.S_IMODE(state.st_mode));entry.update(kind='file',bytes=state.st_size,sha256=sha(target))
     elif stat.S_ISLNK(state.st_mode):entry.update(kind='symlink_metadata_only_no_follow',target=os.readlink(str(path)))
     else:entry['kind']='nonfile_metadata_only'
     members.append(entry)
   rec.setdefault('native_private_evidence',[]).append(dict(original_owner=owner,copy=str(dest),members=members))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text())
  from scripts.unified.generate_mosaic_package import generate
  weights=pathlib.Path(env['mosaic_assets']);input_path=out/'input-small.f32';input_path.write_bytes(b''.join(struct.pack('<f',(i%37-18)*.025) for i in range(288)));model=out/'model-small';rec['model']=generate(weights,weights.parent/'manifest.json',input_path,12,8,64,model)
  for f in model.iterdir():f.chmod(0o400)
  model.chmod(0o500);hunet=out/'hunet-model'
  if mode!='red':
   w=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin');assert sha(w)=='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc';rec['hunet_model']=json.loads(run([sys.executable,'-m','scripts.unified.generate_hunet_package',w,hunet]).strip())
   for f in hunet.iterdir():f.chmod(0o400)
   hunet.chmod(0o500)
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  elfs=[out/'test_native_daemon'] if mode=='red' else [out/'test_native_daemon',out/'test_native_daemon_hunet',out/'test_daemon',out/'test_preparation',out/'vxgpud']
  if mode=='cross':
   prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib);cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  run(cmd+elfs+['-j'+str(jobs)])
  if mode=='cross':
   needed=set();versions=set()
   for target in elfs:
    raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183;text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   mosaic_catalog=out/'mosaic-owner-catalog.bin';run(cmd+[mosaic_catalog,'-j1']);text=run([elfs[0],model,mosaic_catalog,selected_directory('.')/'code.bin',selected_directory('.')/'rodata.bin',out/'mosaic-native-service','red' if mode=='red' else 'normal'],1 if mode=='red' else 0)
   if mode=='red':assert 'U20 sealed native installation must decode exact family and hardware' in text;rec['functional_red']='SEALED_NATIVE_INSTALLATION_REGISTRY_NOT_IMPLEMENTED'
   else:
    hunet_catalog=out/'hunet-owner-catalog.bin';run(cmd+[hunet_catalog,out/'catalog-fixture.bin','-j1']);run([elfs[1],hunet,hunet_catalog,selected_hunet_directory('.')/'code.bin','unused',out/'hunet-native-service','normal'])
    run([out/'test_preparation'])
    for case in ('lifecycle','lifecycle-activate','lifecycle-quiesce','lifecycle-display','lifecycle-resources','shutdown-unreaped','shutdown-held','slow-preparation','close-preparation','identity-preparation','error-preparation','throw-preparation','deferred','legacy','fault'):run([out/'test_daemon',out/'catalog-fixture.bin',out/('journal-'+case),case])
    run(cmd+['test-unified-daemon-cli','-j1'])
    for app in ('mosaic','hunet'):
     records=[json.loads(p.read_text()) for p in (out/(app+'-native-service/journal')).glob('*.json')];startup=[r for r in records if r.get('kind')=='startup'];assert len(startup)==1 and startup[0]['applications']==['installed-nn'] and len(startup[0]['installation_sha256'])==64 and startup[0]['gpu_raw_cycles'] is None
     copies=[e for e in rec['native_private_evidence'] if any(m.get('sha256')==startup[0]['installation_sha256'] and m['path']=='installation.json' for m in e['members'])];assert len(copies)==1;rec.setdefault('startup_installation_binding',[]).append(dict(app=app,installation_sha256=startup[0]['installation_sha256'],private_evidence=copies[0]['copy'],scope='HOST_CONTROL_NOT_GPU'))
     preparation=[r for r in records if r.get('kind')=='preparation'];assert len(preparation)==1 and preparation[0]['end_ns']>=preparation[0]['begin_ns']>0 and preparation[0]['gpu_raw_cycles'] is None and preparation[0]['error'] is None
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
