"""Bound output staging/native service roots; host OS fixtures, no board access."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha

def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U20_COMMON_RESULT_DISPLAY_RUNTIME_ROOT_COMPONENTS_NOT_NATIVE_APPS_GPU_OR_FULL_TASK',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv):
  argv=list(map(str,argv));begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout)
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-3500:],flush=True)
  assert p.returncode==0,'natural exit '+str(p.returncode)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   drm_receipt=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_receipt.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a'
   assert sha(drm_receipt)=='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' and drm['natural_exit']==0 and sha(drm_lib)==drm['dependencies']['drm-target/lib/libdrm.a']['sha256']==env['drm_archive_sha256']
   rec['drm_dependency']=dict(receipt=str(drm_receipt),receipt_sha256=sha(drm_receipt),library=artifact(drm_lib),scope='MATCHED_TARGET_LIBRARY_NOT_UPSTREAM_TARBALL')
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  names=['test_runtime','test_runtime_alloc','test_daemon','test_result_stage','test_stage_display','test_service','test_service_alloc','test_journal','test_journal_runtime','test_display','test_page_switch','test_display_native','test_display_pages','test_display_hunet']
  elfs=[out/n for n in names];run(cmd+elfs+[out/'catalog-fixture.bin','-j'+str(jobs)])
  for elf in elfs:
   raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for elf in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   targets=['test-unified-runtime-stage','test-unified-runtime','test-unified-runtime-residency','test-unified-runtime-binding','test-unified-runtime-alloc','test-unified-runtime-bad-output','test-unified-runtime-late-cancel','test-unified-runtime-expiry','test-unified-runtime-error','test-unified-runtime-quarantine','test-unified-runtime-exception','test-unified-runtime-error-evidence','test-unified-runtime-error-log-retry','test-unified-daemon','test-unified-daemon-fault','test-unified-daemon-legacy','test-unified-daemon-deferred','test-unified-result-stage','test-unified-stage-display','test-unified-service','test-unified-service-alloc','test-unified-display','test-unified-page-switch','test-unified-journal','test-unified-journal-runtime']
   run(cmd+targets+['-j1'])
   run([out/'test_journal',out/'journal-stage','stage'])
   for case in ('reject','evidence','allocation','scope'):run([out/'test_display',case])
   for case in ('caps','lifetime','lifetime-failure','path'):run([out/'test_display_native',case])
   run([out/'test_display_pages']);run([out/'test_display_hunet'])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
