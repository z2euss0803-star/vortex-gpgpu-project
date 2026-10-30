"""Actual common ResultStore and C display engine; fake OS KMS, never a board."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha

def main():
 name,mode=sys.argv[1:];assert mode in ('red','clock-red','reject-red','evidence-red','pages-red','remaining-red','allocation-red','normal','sanitize','thread','cross')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U18_ACTUAL_C_DISPLAY_AND_RESULT_LEASES_FAKE_KMS_NOT_NATIVE_DP',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout)
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4500:],flush=True)
  assert p.returncode==expected,'natural exit '+str(p.returncode)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix']
   assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'))
   assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   drm_receipt=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_receipt.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a'
   assert sha(drm_receipt)=='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' and drm['natural_exit']==0
   assert sha(drm_lib)==drm['dependencies']['drm-target/lib/libdrm.a']['sha256']==env['drm_archive_sha256']
   rec['drm_dependency']=dict(receipt=str(drm_receipt),receipt_sha256=sha(drm_receipt),library=artifact(drm_lib),headers=[artifact(p) for p in sorted((drm_prefix/'include').rglob('*.h'))],scope='EXISTING_MATCHED_TARGET_LIBRARY_NOT_UPSTREAM_TARBALL_HASH')
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  elfs=[out/'test_display',out/'test_page_switch',out/'test_display_pages',out/'test_display_hunet',out/'test_display_native'];run(cmd+elfs+['-j'+str(jobs)])
  regression=mode in ('normal','sanitize','thread','cross')
  if regression:
   shared=[out/'test_service',out/'test_service_alloc'];run(cmd+shared+['-j'+str(jobs)])
   old=['test-demo-kms','test-demo-present','test-demo-hunet-frame']
   old_names=['test_kms','test_present','test_viewer_hunet_frame']
   if mode!='cross':old.insert(0,'test-demo-native-restore');old_names.insert(0,'test_native_restore')
   else:rec['native_restore_regression_scope']='ORIGINAL_INJECTED_LIBDRM_FIXTURE_HOST_NORMAL_ASAN_TSAN_ONLY; PRODUCTION_NATIVE_CPP_AND_KMS_C_LINK_REAL_MATCHED_TARGET_LIBDRM'
   demo=['make','-f','Makefile.demo','OUT='+str(out),'PYTHON=python3','DEMO_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent if mode=='cross' else '/home/z2euss/workspace/mosaic_hunet_dp_20261018/deps/json-c-0.18-vxl1')]
   if mode=='cross':demo+=['CC='+prefix+'gcc','DEMO_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'DEMO_DRM_PREFIX='+str(drm_prefix)]
   elif mode in ('sanitize','thread'):demo+=['CC=gcc -fsanitize='+('address,undefined' if mode=='sanitize' else 'thread')+' -fno-omit-frame-pointer']
   old_elfs=[out/'demo'/n for n in old_names];run(demo+old_elfs+['-j'+str(jobs)])
   elfs+=shared+old_elfs
  for elf in elfs:
   raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for elf in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available)
   rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  elif mode=='red':
   assert 'U18_DISPLAY_ASSERT one KMS owner must be acquired' in run([elfs[0]],1)
   assert 'U18_DISPLAY_ASSERT page switch owner' in run([elfs[1]],1)
   rec['functional_red']='SINGLE_KMS_COMMON_RESULT_DISPLAY_FACTORY_MISSING'
  elif mode=='clock-red':
   for elf in elfs:
    text=run([elf],1);assert 'U18_ACTUAL_ERROR code=22 category=DISPLAY detail=presentation result persistence failed' in text
   rec['functional_red']='HOST_DISPLAY_REJECTED_BY_EXISTING_LINUX_ONLY_RESULT_MARKER'
  elif mode=='reject-red':
   text=run([elfs[0],'reject'],1);assert 'U18_DISPLAY_ASSERT rejected frame leaves no pending display or common lease' in text
   rec['functional_red']='PRE_SUBMIT_REJECTION_LEFT_OBSERVATION_PENDING'
  elif mode=='evidence-red':
   text=run([elfs[0],'evidence'],1);assert 'U18_DISPLAY_ASSERT first evidence error identity must be retained' in text
   rec['functional_red']='ORIGINAL_JOURNAL_ERROR_IDENTITY_REWRITTEN'
  elif mode=='pages-red':
   text=run([elfs[2]],1);assert 'U18_DISPLAY_ASSERT P8 native page layout required' in text
   rec['functional_red']='NATIVE_RASTER_PAGE_LAYOUT_AND_COMPOSITION_MISSING'
  elif mode=='remaining-red':
   text=run([elfs[3]],1);assert 'U18_DISPLAY_ASSERT HUNet dedicated bound panel required' in text
   for case,assertion in [('caps','actual selected caps must replace requested dimensions'),('lifetime','KMS context lifetime must follow display owner'),('path','native factory must call original path admission without opening a device')]:
    text=run([elfs[4],case],1);assert 'U18_DISPLAY_ASSERT '+assertion in text
   rec['functional_red']='HUNET_PANEL_AND_NATIVE_CAPS_CONTEXT_FACTORY_MISSING'
  elif mode=='allocation-red':
   text=run([elfs[0],'allocation'],1);assert 'U18_DISPLAY_ASSERT OOM before KMS submit leaves no false pending or lease' in text
   rec['functional_red']='PRE_SUBMIT_ALLOCATION_LEFT_FALSE_PENDING'
  else:
   run(cmd+['test-unified-display','test-unified-page-switch','-j1'])
   run([elfs[0],'reject'])
   run([elfs[0],'evidence'])
   run([elfs[0],'allocation'])
   run([elfs[0],'scope'])
   run([elfs[2]])
   run([elfs[3]])
   for case in ('caps','lifetime','lifetime-failure','path'):run([elfs[4],case])
   run(cmd+['test-unified-service','test-unified-service-alloc','-j1'])
   run(demo+old+['-j1'])
   rec['regressions']='ORIGINAL_NATIVE_RESTORE_KMS_PRESENT_HUNET_FRAME_AND_SHARED_RESULT_OWNER_ALLOC'
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
