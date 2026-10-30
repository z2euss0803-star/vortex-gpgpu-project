"""U11 real mathematical source + immutable package planning; no GPU/board access."""
import hashlib,json,os,pathlib,re,stat,struct,subprocess,sys,time
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def artifact(p):
 s=p.lstat();row=dict(path=str(p),bytes=s.st_size,mode=stat.S_IMODE(s.st_mode))
 if stat.S_ISLNK(s.st_mode):row.update(kind='symlink',link=os.readlink(str(p)))
 elif stat.S_ISFIFO(s.st_mode):row.update(kind='fifo') # Required hostile-loader fixture; never open it.
 elif stat.S_ISREG(s.st_mode):row.update(kind='file',sha256=sha(p))
 else:raise ValueError('unrecognized artifact type '+str(p))
 return row
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','cross');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(name=name,mode=mode,scope='HOST_NATIVE_MATH_PE_PROTOCOL_MODEL_AND_REAL_PACKAGE_PLAN' if mode!='cross' else 'AARCH64_LINK_NOT_EXECUTED',gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv):
  begin=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-5000:],flush=True)
  assert not p.returncode,'natural exit '+str(p.returncode)
  assert not any(x in p.stdout for x in (b'ERROR: AddressSanitizer',b'runtime error:',b'U11_MODEL_ASSERT',b'U11_INT16_ASSERT',b'U07_ASSERT'))
  return p.stdout.decode('utf8','replace')
 try:
  family_path=pathlib.Path('data/nnrt/programs/int16-family-v1');family=json.loads((family_path/'family.json').read_text())
  for row in family['source_rows']:assert sha(row['path'])==row['sha256'],'family source drift '+row['path']
  assert sha(family_path/'code.bin')==family['code_sha256'] and sha(family_path/'args-schema.bin')==family['schema_sha256'];rec['family']=family
  elfs=['test_int16_ops','test_int16_entry','test_int16_registry','test_int16_models','test_nn_package','test_nn_registry','test_nn_graph','test_nn_memory_plan']
  command=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];targets=['test-unified-int16','test-unified-int16-entry','test-unified-int16-registry','test-unified-extension-plan','test-unified-package','test-unified-registry','test-unified-graph','test-unified-memory-plan','test-unified-package-legacy']
  if mode=='sanitize':command+=['SANITIZE=address,undefined']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']);assert run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dependency=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dependency.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];jp=pathlib.Path(lib['path']).parent.parent
   rec['json_dependency']=dict(receipt=str(dependency),receipt_sha256=sha(dependency),library=lib)
   command+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(jp)];run(command+[str(out/n) for n in elfs]+['-j'+str(jobs)]);needed=set();versions=set()
   for n in elfs:
    p=out/n;raw=p.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',p]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions));rec['executed_target_elfs']=False
  else:
   run(command+[str(out/n) for n in elfs]+['-j'+str(jobs)]);run(command+targets+['-j1']);dry=run(command+targets+['-n','-j1']);assert not re.search(r'(?:g\+\+|gcc|cc).* -(?:c|o) ',dry),'matched OUT recompiled'
   rec['executed_target_elfs']=False;rec['new_model_values_exact']=[64,128];rec['new_model_jobs']=[2,3];rec['semantic_negative_packages']=13;rec['legacy_packed48_six_bytes']=True
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    p=pathlib.Path(folder)/n
    if n!='receipt.json':rec['artifacts'].append(artifact(p))
  rec['natural_exit']=0;print('U11_'+mode.upper()+'_SOFTWARE_PASS_GPU_CYCLES_NULL',flush=True)
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
