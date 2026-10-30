"""Fresh pinned json-c + U07 host verification or matching AArch64 link.

Invoked only inside run_on_ubuntu's three C locks/resource gate. Never opens a
device, target executable, SD, kernel image, or PL control interface.
"""
import hashlib,json,os,pathlib,re,shlex,struct,subprocess,sys,time
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 name=sys.argv[1];mode=sys.argv[2];out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 deps=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/deps');source=deps/'json-c-018-vxl1-build/source'
 record=dict(name=name,mode=mode,gpu_raw_cycles=None,scope='UBUNTU_SOFTWARE' if mode!='cross' else 'AARCH64_LINK_NOT_EXECUTED',commands=[],artifacts=[])
 def run(argv,cwd=None,env=None):
  start=time.monotonic();p=subprocess.run([str(x) for x in argv],cwd=str(cwd) if cwd else None,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  log=out/('command-%03d.log'%len(record['commands']));log.write_bytes(p.stdout);record['commands'].append(dict(argv=[str(x) for x in argv],cwd=str(cwd or pathlib.Path.cwd()),natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)))
  (out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-5000:],flush=True)
  if p.returncode:raise RuntimeError(str(log)+' exit='+str(p.returncode))
  return p.stdout.decode('utf8','replace')
 try:
  assert mode in ('normal','sanitize','cross')
  assert sha(deps/'json-c-0.18.tar.gz')=='876ab046479166b869afc6896d288183bbc0e5843f141200c677b3e8dfb11724'
  assert sha(source/'json_tokener.c')=='5cfe8ffcf4e8cb75aeda1d727d1728f9dcfef62f1bd0b3b9e702751dbfea0479'
  rows=[(str(p.relative_to(source)),sha(p)) for p in sorted(source.rglob('*')) if p.is_file()];record['json_source_identity']=hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
  prefix='';extra=[]
  if mode=='cross':
   envdata=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=envdata['aarch64_prefix']
   record['toolchain']=run([prefix+'g++','--version']);assert '9.2.0' in record['toolchain'];assert run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   adapter=out/'target-ranlib';adapter.write_text('#!/bin/sh\nexec '+shlex.quote(prefix+'ar')+' s "$@"\n');adapter.chmod(0o755)
   extra=['-DCMAKE_SYSTEM_NAME=Linux','-DCMAKE_SYSTEM_PROCESSOR=aarch64','-DCMAKE_C_COMPILER='+prefix+'gcc','-DCMAKE_AR='+prefix+'ar','-DCMAKE_RANLIB='+str(adapter.resolve())]
  if mode=='sanitize':extra+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer']
  jb=out/'json-build';jb.mkdir();jp=(out/'json-install').resolve();native_env=dict(os.environ,LD_LIBRARY_PATH=str(deps/'native/usr/lib/x86_64-linux-gnu'))
  run([deps/'native/usr/bin/cmake',source,'-DBUILD_TESTING=OFF','-DBUILD_SHARED_LIBS=OFF','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(jp)]+extra,jb,native_env)
  run(['make','-j1'],jb,native_env);run(['make','install'],jb,native_env)
  opts=['OUT='+str(out/'objects'),'NN_JSON_PREFIX='+str(jp)];san=['SANITIZE=address,undefined'] if mode=='sanitize' else []
  targets=['test-unified-package','test-unified-registry','test-unified-graph','test-unified-memory-plan']
  if mode!='cross':
   run(['make','-f','Makefile.unified']+opts+san+targets+['test-unified-package-legacy','-j1'])
   run(['make','-f','Makefile.unified','OUT='+str(out/'backend-regression')]+san+['test-unified-backend','test-unified-resources','test-v2-control','test-v2-provider','-j1'])
  else:
   run(['make','-f','Makefile.unified']+opts+['CC='+prefix+'gcc','CXX='+prefix+'g++']+[str(out/'objects'/('test_nn_'+n)) for n in ('package','registry','graph','memory_plan')]+['-j1'])
   needed=set();required=set()
   for n in ('package','registry','graph','memory_plan'):
    p=out/'objects'/('test_nn_'+n);raw=p.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    readelf=run([prefix+'readelf','-hW','-dW','-VW',p]);(out/(n+'.readelf')).write_text(readelf)
    needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',readelf));required.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',readelf))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()))
   from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,versions=runtime_inventory(pathlib.Path(envdata['rootfs']),needed,runtime,prefix+'readelf')
   assert required<=set(versions),'rootfs symbol version mismatch: '+repr(required-set(versions))
   record['target_runtime']=dict(rootfs_sha256=sha(envdata['rootfs']),needed=sorted(needed),required_versions=sorted(required),files=files)
  for p in [jp/'lib/libjson-c.a']+list((out/'objects').glob('test_nn_*')):record['artifacts'].append(dict(path=str(p.resolve()),bytes=p.stat().st_size,sha256=sha(p)))
  record['natural_exit']=0;print('U07_'+mode.upper()+'_PASS_GPU_CYCLES_NULL',flush=True)
 except Exception as e:record.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
 return 0
if __name__=='__main__':sys.exit(main())
