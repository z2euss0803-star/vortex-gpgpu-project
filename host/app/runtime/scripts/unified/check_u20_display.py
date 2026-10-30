"""Actual Runtime/private-stage/original C KMS host flow, never a DP claim."""
import pathlib,subprocess,sys,os,json,time,re,struct
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U20_COMMON_RUNTIME_PRESENTATION_IMAGE_COMPONENTS_NOT_FULL_U20_GPU_DP',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0,compiler=False,limit=90):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));begin=time.monotonic()
  with log.open('xb') as f:
   try:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if compiler else limit)
   except subprocess.TimeoutExpired:
    rec['commands'].append(dict(argv=argv,natural_exit=None,controller_timeout_s=limit,termination='SUBPROCESS_RUN_STOPS_OWN_DIRECT_HOST_CHILD',elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));raise
  text=log.read_text(errors='replace');rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));print(text[-1800:],flush=True);assert p.returncode==expected;assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  exe=out/'test_native_display';elfs=[exe]
  if mode!='red':elfs += [out/n for n in ('test_image_service','test_image_udp','test_display','test_page_switch','test_display_pages','test_display_hunet','test_display_native','test_stage_display','test_result_stage','test_runtime')]
  if mode!='red':elfs += [out/n for n in ('test_image_owner','test_compute_port','test_deferred_port','test_native_router','test_daemon','test_preparation')]
  if mode!='red':elfs += [out/'test_port_result_alloc']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix']
   assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   drm_receipt=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_receipt.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a'
   assert sha(drm_receipt)=='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' and drm['natural_exit']==0 and sha(drm_lib)==drm['dependencies']['drm-target/lib/libdrm.a']['sha256']==env['drm_archive_sha256']
   rec['drm_dependency']=dict(receipt_sha256=sha(drm_receipt),library=artifact(drm_lib));cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  run(cmd+elfs+['-j'+str(jobs)],compiler=True)
  for target in elfs:
   raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='red':
   text=run([exe,'flow'],1);assert 'U20 actual common Runtime/DP presentation bridge required' in text;rec['functional_red']='COMMON_PRESENTATION_BINDING_ENOSYS'
  elif mode=='cross':
   needed=set();versions=set()
   for target in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   run(cmd+[out/'catalog-fixture.bin','-j1'],compiler=True)
   for case in ('flow','roots','error'):run([exe,case])
   for case in ('flow','wrong-style','initial-blank'):run([out/'test_image_service','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case])
   run([out/'test_image_udp'])
   for case in (None,'reject','evidence','allocation','scope'):run([out/'test_display']+([case] if case else []))
   for binary in ('test_page_switch','test_display_pages','test_display_hunet'):run([out/binary])
   for case in ('caps','lifetime','lifetime-failure','path'):run([out/'test_display_native',case])
   run([out/'test_stage_display']);run([out/'test_stage_display','cookie']);run([out/'test_result_stage'])
   for case in ('flow','limits','stage','residency','residency-reentrant'):run([out/'test_runtime',case])
   for case in ('flow','display-retire','guard','trace-error','final-trace-error','publish-error','binding','binding-zero','cancel','cancel-mid'):run([out/'test_image_owner','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case])
   for case in ('flow','ack-log','timeout','ack-fault','timeout-evidence','bad-output','finish-throw'):run([out/'test_compute_port',out/'catalog-fixture.bin',case])
   for case in ('flow','stale','prepare-error','cookie','epoch','prepare-throw','missing-hook','cancel'):run([out/'test_deferred_port',out/'catalog-fixture.bin',case])
   for case in ('normal','fault','allocation','concurrency'):run([out/'test_native_router',out/'catalog-fixture.bin',case])
   for case in ('lifecycle','lifecycle-activate','lifecycle-quiesce','lifecycle-display','lifecycle-resources','shutdown-unreaped','shutdown-held','slow-preparation','close-preparation','identity-preparation','error-preparation','throw-preparation','deferred','legacy','fault'):run([out/'test_daemon',out/'catalog-fixture.bin',out/('journal-'+case),case],limit=240)
   run([out/'test_preparation'])
   run([out/'test_port_result_alloc',out/'catalog-fixture.bin'])
   rec['regressions']='IMAGE_REENTRY_CURRENT_PENDING_EXECUTE_GUARDS_PLUS_PRIOR_DISPLAY_IMAGE_UDP_AND_SHARED_COMPUTE_DEFERRED_ROUTER_DAEMON_PREPARATION_LIFETIME_ERROR_SUITE'
  rec['natural_exit']=0
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
