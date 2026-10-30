"""U12 partial math gate: actual source/order, not a full migration/application PASS."""
import hashlib,json,os,pathlib,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(name=name,mode=mode,scope='U12_MATH_COMPONENT_NOT_FULL_MIGRATION_NOT_GPU',gpu_raw_cycles=None,commands=[],artifacts=[],executed_target_elfs=False)
 def run(argv):
  begin=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout)
  rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4000:],flush=True)
  assert not p.returncode,'natural exit '+str(p.returncode)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','runtime error:','U12_LAYOUT_ASSERT','U12_SCALAR_ASSERT'))
  return text
 try:
  run([sys.executable,'-m','scripts.unified.generate_hunet_scalar','--check'])
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elfs=['test_hunet_layout','test_hunet_scalar']
  if mode=='sanitize':cmd+=['SANITIZE=address,undefined']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix']
   assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++'];run(cmd+[str(out/n) for n in elfs]+['-j1'])
   needed=set();versions=set();import re
   for n in elfs:
    raw=(out/n).read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',out/n]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available)
   rec['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions))
  else:
   weights=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin')
   assert sha(weights)=='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc','original immutable HUNet weights required'
   package=out/'actual-model'
   generated=run([sys.executable,'-m','scripts.unified.generate_hunet_package',weights,package])
   rec['model_package']=json.loads(generated)
   rec['original_weights_sha256']=sha(weights)
   for p in package.iterdir():p.chmod(0o400)
   package.chmod(0o500)
   text=run(cmd+['test-unified-hunet','-j1'])
   assert 'U12_PACK_35_STAGES_314_DESCRIPTORS_3266688_VALUES_OLD_PATH_EXACT_NOT_GPU' in text
   assert 'U12_SCALAR_68_FROZEN_JOBS_PLUS_COPY_2814287_VALUES_BITWISE_LEGACY_NOT_GPU' in text
   for n in elfs:
    raw=(out/n).read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==62
   rec.update(pack_stages=35,descriptors=314,packed_values=3266688,scalar_jobs=68,scalar_values_including_copy=2814287)
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  rec['natural_exit']=0;print('U12_'+mode.upper()+'_MATH_COMPONENT_PASS_NOT_APPLICATION_GPU_CYCLES_NULL',flush=True)
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
