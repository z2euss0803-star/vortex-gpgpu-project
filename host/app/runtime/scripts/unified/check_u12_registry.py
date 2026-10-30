"""Source-bound registered HUNet metadata gates, not full graph execution."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
from scripts.unified.hunet_program import selected_directory
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','cross');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U12_REGISTERED_OPERATOR_METADATA_NOT_MODEL_EXECUTION',gpu_raw_cycles=None,commands=[],artifacts=[],mode=mode,compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4000:],flush=True);assert p.returncode==0,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','runtime error:','U12_REGISTRY_ASSERT'));return text
 try:
  directory=selected_directory('.');family=json.loads((directory/'family.json').read_text());assert sha(directory/'code.bin')==family['code_sha256'] and sha(directory/'args-schema.bin')==family['schema_sha256']
  for row in family['sources']:assert sha(row['path'])==row['sha256']
  rec['program']=family;cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_hunet_registry'
  if mode=='sanitize':cmd+=['SANITIZE=address,undefined']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  run(cmd+[elf,'-j'+str(jobs)]);raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed=set(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions=set(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text));sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:assert 'U12_REGISTERED_HUNET_103_GPU_13_CPU_METADATA_DISPATCH_PASS_NOT_GPU_EXECUTION' in run(cmd+['test-unified-hunet-registry','-j1'])
  rec['natural_exit']=0;print('U12_'+mode.upper()+'_REGISTERED_OPERATOR_COMPONENT_PASS_NOT_FULL_MIGRATION')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
