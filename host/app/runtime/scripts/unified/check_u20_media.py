"""Actual native media factories; injected C/oracles/OS remain HOST only."""
import pathlib,subprocess,json,sys,time,os,struct,re
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','declared','normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='ACTUAL_NATIVE_MEDIA_FACTORIES_HOST_C_CONTROL_ORACLES_FAKE_KMS_NOT_GPU_BOARD',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0,compiler=False):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));begin=time.monotonic()
  with log.open('xb') as stream:p=subprocess.run(argv,stdout=stream,stderr=subprocess.STDOUT,timeout=None if compiler else 180)
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=log.read_text(errors='replace');print(text[-2500:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(t in text for t in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix']
   assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   drm_receipt=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_receipt.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a'
   assert sha(drm_receipt)=='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' and drm['natural_exit']==0 and sha(drm_lib)==drm['dependencies']['drm-target/lib/libdrm.a']['sha256']==env['drm_archive_sha256']
   rec['drm_dependency']=dict(receipt_sha256=sha(drm_receipt),library=artifact(drm_lib));cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  targets=[out/'test_native_image_media',out/'test_native_p8_media'];extra=[out/'test_media_image_adapter',out/'test_media_p8_adapter'] if mode!='red' else []
  if mode not in ('red','declared'):extra+=[out/'test_media_image_owner',out/'test_media_p8_owner']
  build_log=run(cmd+targets+extra+['-j'+str(jobs)],compiler=True)
  common=('unified/image_request.cpp','p8/request.cpp','image/platform_unified.cpp','unified/catalog.cpp','unified/residency.cpp')
  counts={p:sum((' -c '+p+' -o ') in line for line in build_log.splitlines()) for p in common};assert all(n<=1 for n in counts.values())
  assert all(' -c ' in line or not re.search(r'\s[^ ]+\.cpp\s',line) for line in build_log.splitlines() if line.startswith('g++ '))
  rec['shared_compile']=dict(compile_counts=counts,no_monolithic_cpp_recompilation=True,cache_proof_present=(out/'verified-object-cache.json').is_file(),test_results_reused=False)
  for target in targets+extra:
   raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for target in targets+extra:
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False,natural_exit=0,scope='SIX_MATCHED_AARCH64_NATIVE_MEDIA_AND_LEGACY_ELFS_NOT_EXECUTED')
   return
  for kind,exe,catalog,programs,table in [('IMAGE',targets[0],'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin'),('P8',targets[1],'data/p8/programs/catalog.bin','data/p8/programs','unused')]:
   for case in (('red','declared') if mode=='red' else ('declared',) if mode=='declared' else ('prepare','flow','guard','declared')):
    text=run([exe,catalog,programs,table,out/(kind.lower()+'-'+case),case],1 if mode=='red' else 0)
    if mode=='red':assert (('U20 actual '+kind+' native media factory must use sole owner and shared budget') if case=='red' else ('U20 declared native '+kind)) in text
  if mode=='red':rec['functional_red']=['ACTUAL_IMAGE_NATIVE_FACTORY_ENOSYS','ACTUAL_P8_NATIVE_FACTORY_ENOSYS','CHECKED_NATIVE_IMAGE_SESSION_MISSING','CHECKED_NATIVE_P8_METADATA_ADAPTER_MISSING']
  if mode!='red':
   run([out/'test_media_image_adapter','data/image/programs-v2/catalog.bin']);run([out/'test_media_p8_adapter','data/p8'])
   rec['legacy_admission']='ORIGINAL_SCOPE_AND_PROVIDER_NEGATIVES_UNCHANGED'
  if mode not in ('red','declared'):
   for case in ('flow','guard','cancel','trace-error','display-retire'):
    run([out/'test_media_image_owner','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case])
   for case in ('flow','guard','cancel','trace','callback-snapshot'):
    run([out/'test_media_p8_owner','data/p8/programs/catalog.bin','data/p8/programs',case])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for p in out.rglob('*'):
   if p.is_file() and p.name!='receipt.json':rec['artifacts'].append(artifact(p))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
