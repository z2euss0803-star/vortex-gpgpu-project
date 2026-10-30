"""Actual C owner plus frozen instruction model; no physical GPU or DP evidence."""
import json,os,pathlib,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross','red');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U14_NATIVE_IMAGE_OWNER_FROZEN_WORDS_HOST_NOT_GPU_OR_DISPLAY',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4000:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','U14_IMAGE_ASSERT'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_image_owner_pixels'
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
  else:
   from scripts.unified.verify_image_outputs import verify
   if mode=='red':
    import hashlib
    source=pathlib.Path('/home/z2euss/workspace/image_studio_linux_20261021/input')
    prefix=source/'reports/v23-image-studio-720p-fast-20261016'
    master=(source/'MANIFEST_SHA256.json').read_bytes();assert hashlib.sha256(master).hexdigest()=='96dc7902f3b06d9047d1c6d511ec8ca6ee1a6c96021420dc75ebf825c2f415e3'
    for row in json.loads(master)['files']:
     if row['path'] in ('reports/v23-image-studio-720p-fast-20261016/pipeline-colors/input.rgba','reports/v23-image-studio-720p-fast-20261016/pipeline-colors/oracle.json','gpu_programs/tables-64KiB.bin'):
      blob=(source/row['path']).read_bytes();assert len(blob)==row['bytes'] and hashlib.sha256(blob).hexdigest()==row['sha256']
    dest=out/'first';dest.mkdir()
    text=run([elf,'first',prefix/'pipeline-colors/input.rgba',prefix/'replay/requests.txt',dest,source/'gpu_programs/tables-64KiB.bin'])
    oracle=json.loads((prefix/'pipeline-colors/oracle.json').read_bytes());expected=next(x['sha256'] for x in oracle['results'] if x['style']==3 and x['preset']==0)
    actual=hashlib.sha256((dest/'style-3-p0.rgba').read_bytes()).hexdigest()
    assert actual!=expected,'RED must witness an actual output mismatch before enabling instruction model'
    rec['functional_red']=dict(actual=actual,expected=expected,reason='HOST_MARKER_IS_NOT_ORIGINAL_ARITHMETIC_OUTPUT')
    print('U14_COMPILED_NATIVE_OWNER_OUTPUT_MISMATCH_OBSERVED_EXPECTED_RED_NOT_ACCEPTANCE')
   else:
    rec['oracle_proof']=verify(elf,out,run)
    rec['oracle_proof']['scope']='ACTUAL_C_SHARED_OWNER_FROZEN_RV32_HOST_INSTRUCTION_MODEL_NOT_GPU'
  rec['natural_exit']=0;print('U14_'+mode.upper()+'_IMAGE_OWNER_PIXELS_COMPLETED_HOST_NOT_GPU_OR_BOARD')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
