"""Actual daemon/common worker and UDP, using HOST C protocol and injected KMS."""
import pathlib,subprocess,sys,json,time,os,struct,re
from scripts.unified.check_u11 import artifact,sha
from scripts.unified.capture_native_private import capture
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='NATIVE_SCENE_STORAGE_AND_OLD_MEDIA_HOST_CONTROL_NOT_GPU_DP',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[:3]==['make','-f','Makefile.unified'] else 600 if pathlib.Path(argv[0]).name=='test_native_p8_daemon' else 180)
  rec['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=log.read_text(errors='replace');print(text[-2000:],flush=True)
  private=capture(text,out,len(rec['commands']))
  if private:rec.setdefault('native_private_evidence',[]).extend(private)
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
  assert r.returncode==expected;assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','NATIVE_FILES_SEAL_ERROR'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   drm_receipt=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_receipt.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a';assert sha(drm_receipt)=='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' and drm['natural_exit']==0 and sha(drm_lib)==drm['dependencies']['drm-target/lib/libdrm.a']['sha256']==env['drm_archive_sha256'];rec['drm_dependency']=dict(receipt_sha256=sha(drm_receipt),library=artifact(drm_lib));cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  elf=out/'test_native_image_daemon';extra=[] if mode=='red' else [out/x for x in ('test_native_image_ingress','test_native_image_media','test_native_p8_media','test_media_image_adapter','test_media_p8_adapter','test_media_image_owner','test_media_p8_owner','test_media_daemon','test_media_image_service','test_native_p8_texture_ingress')]
  extra+=[out/x for x in ('test_native_p8_service','test_native_p8_daemon','test_host_memory','test_native_files','test_runtime','test_runtime_alloc','test_preparation','test_pinned_exit','test_texture_source')]
  fixture=[] if mode=='red' else [out/'catalog-fixture.bin'];run(cmd+[elf]+extra+fixture+['-j'+str(jobs)])
  for binary in [elf]+extra:
   raw=binary.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for binary in [elf]+extra:
    text=run([prefix+'readelf','-hW','-dW','-VW',binary]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False,natural_exit=0,scope='TWENTY_MATCHED_AARCH64_NATIVE_SCENE_STORAGE_AND_LEGACY_ELFS_NOT_EXECUTED');return
  text=run([elf,'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/'daemon'],1 if mode=='red' else 0)
  if mode=='red':assert 'U20 actual native UDP service must join daemon single worker' in text
  else:
   assert 'U20_NATIVE_IMAGE_DAEMON_ACTUAL_UDP_SINGLE_WORKER_FULL720_PASS_HOST_NOT_GPU_DP' in text
   partial=run([elf,'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/'partial','partial']);assert 'U20_NATIVE_IMAGE_DAEMON_PARTIAL_UPLOAD_SHUTDOWN_PASS_HOST_NOT_GPU_DP' in partial
   for case in ('declared','ingress'):run([extra[0],'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/case,case])
   for case in ('declared','texture-ingress','wire-ingress','wire-cancel','wire-abandon'):run([out/'test_native_p8_texture_ingress','data/p8/programs/catalog.bin','data/p8/programs','unused',out/('native-texture-'+case),case])
   for kind,binary,catalog,programs,table in [('image','test_native_image_media','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin'),('p8','test_native_p8_media','data/p8/programs/catalog.bin','data/p8/programs','unused')]:
    for case in ('prepare','flow','guard','declared'):run([out/binary,catalog,programs,table,out/(kind+'-'+case),case])
   run([out/'test_media_image_adapter','data/image/programs-v2/catalog.bin']);run([out/'test_media_p8_adapter','data/p8'])
   for case in ('flow','guard','cancel','trace-error','display-retire'):run([out/'test_media_image_owner','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case])
   for case in ('flow','guard','cancel','trace','callback-snapshot'):run([out/'test_media_p8_owner','data/p8/programs/catalog.bin','data/p8/programs',case])
   for case in ('normal','fault','legacy','deferred','lifecycle','slow-preparation','close-preparation','identity-preparation','error-preparation','throw-preparation','shutdown-unreaped','shutdown-held'):run([out/'test_media_daemon',out/'catalog-fixture.bin',out/('old-daemon-'+case),case])
   for case in ('normal','initial-blank','wrong-style','oom','acquire-oom'):run([out/'test_media_image_service','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case])
  run([out/'test_native_p8_service','data/p8/programs/catalog.bin','data/p8/programs','unused',out/'original-first-frame','scene-service'])
  for case in ('normal','partial','fault'):run([out/'test_native_p8_daemon','data/p8/programs/catalog.bin','data/p8/programs','data/p8',out/('native-p8-'+case),case])
  run([out/'test_host_memory']);run([out/'test_host_memory','metadata-budget']);run([out/'test_native_files']);run([out/'test_native_files','declared-context'])
  for case in ('flow','limits','stage','residency','residency-reentrant'):run([out/'test_runtime',case])
  run([out/'test_runtime_alloc']);run([out/'test_preparation'])
  for case in ('result','scheduler','runtime','result-empty','scheduler-empty','runtime-empty'):run([out/'test_pinned_exit',case])
  run([out/'test_texture_source'])
  run([sys.executable,'-m','unittest','discover','-s','tests/unified','-p','test_extension_audit.py','-v'])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
