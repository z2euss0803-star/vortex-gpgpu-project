"""P8 actual common C owner, explicit CPU injection and simulated presentation only."""
import json,os,pathlib,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
from scripts.unified.texture_package import build_package
from scripts.unified.texture_pillow import dependency
import importlib.util
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross','red','pixel-red','startup-red');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U17_TEXTURE_ACTUAL_C_OWNER_CPU_INJECTION_NOT_GPU_OR_NATIVE_HW_RETIREMENT',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4000:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','U16_P8_OWNER_ASSERT'));return text
 try:
  rec['dependency']=dependency();spec=importlib.util.spec_from_file_location('texture_fixture','tests/unified/test_texture_package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.Package().fixture(out/'input');package=build_package(out/'input/result.json',out/'package');os.environ['VX_P8_TEXTURE_PACKAGE']=str(out/'package');os.environ['VX_P8_TEXTURE_ID']=package['package_id']
  cmd=['make' ,'-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_p8_owner'
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib);cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  elfs=[elf];run(cmd+elfs+['-j'+str(jobs)])
  for target in elfs:
   raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for target in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(__import__('re').findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(__import__('re').findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  elif mode=='startup-red':
   text=run([elf,'data/p8/programs/catalog.bin','data/p8/programs','startup-bypass'],expected=1);assert 'U09_ASSERT startup reference cannot be bypassed' in text;rec['functional_red']='STARTUP_REFERENCE_BYPASS_ACCEPTED'
  elif mode=='pixel-red':
   for case in ('pixel','depth'):
    text=run([elf,'data/p8/programs/catalog.bin','data/p8/programs',case],expected=1);assert 'U09_ASSERT incomplete output must stop before publication' in text
   rec['functional_red']='TWO_COMPILED_INCOMPLETE_OUTPUTS_ACCEPTED_WITHOUT_REFERENCE'
  elif mode=='red':
   text=run([elf,'data/p8/programs/catalog.bin','data/p8/programs','texture'],expected=1);assert 'U09_ASSERT lazy native frame owner' in text;rec['functional_red']='COMPILED_TEXTURE_OWNER_ADMISSION_MISSING'
  else:
   for case in ('flow','warrior','guard','projection','trace','publish','display','forged','pixel','depth','cancel','cancel-mid','startup-bypass','texture','texture-warrior'):
    text=run([elf,'data/p8/programs/catalog.bin','data/p8/programs',case]);assert 'U16_P8_OWNER_' in text
  rec['natural_exit']=0;print('U16_'+mode.upper()+'_P8_OWNER_PASS_CPU_INJECTION_NOT_GPU_OR_BOARD')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
