"""Compile complete production runtime and matching test ELFs, never run them.

Invoke through the existing three-lock/resource-gated task runner, worker1.
Only the production installation list is eligible for a later image payload.
"""
import pathlib,json,re,sys,py_compile,shutil,argparse
from scripts.unified.cross_common import sha,snapshot,provider,aarch64_target,CommandLog,verify_source_lock,new_build_output
from scripts.unified.audit_cross import inspect_elf,audit_production_symbols

TARGETS=['vxgpud','test_installed_image','test_installed_p8','test_native_daemon','test_native_daemon_hunet','test_daemon','test_preparation','test_installed_media','test_native_result_view','test_native_result_export','test_native_export_bridge','test_texture_source','test_end_to_end','test_native_int16_extension']

def build_target(env_path,source_lock_path,source_lock_sha256,root,target,out):
 if target not in ('aarch64-s8','aarch64-p8','aarch64-s8-joint'):raise ValueError('explicit isolated AArch64 target required')
 root=pathlib.Path(root).resolve();before=verify_source_lock(root,source_lock_path,source_lock_sha256);out=new_build_output(root,out)
 rec=dict(scope='U22_MATCHED_COMPLETE_AARCH64_RUNTIME_AND_TEST_ELFS_NOT_EXECUTED',target=target,provider=provider(root,{'aarch64-s8':1,'aarch64-p8':2,'aarch64-s8-joint':4}[target]),source_before=before,source_lock=dict(path=str(source_lock_path),sha256=source_lock_sha256),commands=[],artifacts=[],installation=[],gpu_raw_cycles=None,executed_target_elfs=False,board='NOT_RUN',image='NOT_BUILT',compiler_jobs=1)
 log=CommandLog(root,out,rec)
 try:
  env=json.loads(pathlib.Path(env_path).read_text());prefix=env['aarch64_prefix']
  if '9.2.0' not in log.run([prefix+'g++','--version'],'compiler-version') or log.run([prefix+'gcc','-dumpmachine'],'compiler-machine').strip()!='aarch64-xilinx-linux':raise ValueError('matching GCC9.2 AArch64 required')
  rec['tools']={name:dict(path=str(pathlib.Path(prefix+name).resolve()),sha256=sha(pathlib.Path(prefix+name).resolve())) for name in ('gcc','g++','readelf','ld','as')}
  rec['compiler_sysroot']=log.run([prefix+'gcc','-print-sysroot'],'compiler-sysroot').strip()
  jp=root/'build/U07-aarch64-012/receipt.json';dependency=json.loads(jp.read_text());library=next(x for x in dependency['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'))
  if dependency['natural_exit']!=0 or sha(library['path'])!=library['sha256']:raise ValueError('matching target json-c dependency')
  drm_receipt=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_receipt.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a'
  if sha(drm_receipt)!='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' or drm['natural_exit']!=0 or sha(drm_lib)!=drm['dependencies']['drm-target/lib/libdrm.a']['sha256'] or sha(drm_lib)!=env['drm_archive_sha256']:raise ValueError('matching target libdrm dependency')
  rec['static_dependencies']=dict(json=dict(receipt_sha256=sha(jp),library=library),drm=dict(receipt_sha256=sha(drm_receipt),library=str(drm_lib),sha256=sha(drm_lib)))
  build=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1','CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(library['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  log.run(build+[out/name for name in TARGETS]+['-n','-j1'],'plan-all-real-targets-before-compilation')
  log.run(build+[out/name for name in TARGETS]+['-j1'],'compile-complete-runtime-and-tests')
  needed=set();versions=set()
  for name in TARGETS:
   path=out/name;identity=inspect_elf(path,'aarch64',sha(path));text=log.run([prefix+'readelf','-hW','-dW','-VW','-Ws',path],name+'-readelf');needed.update(identity['needed']);versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   if name=='vxgpud':audit_production_symbols(identity)
   rec['artifacts'].append(dict(path=str(path),name=name,bytes=path.stat().st_size,sha256=sha(path),elf=identity,installable=name=='vxgpud',scope='PRODUCTION_RUNTIME_NOT_EXECUTED' if name=='vxgpud' else 'TARGET_TEST_ELF_ONLY_NEVER_INSTALL_AS_APPLICATION'))
  sys.path.insert(0,str(root/'scripts/hunet'));from cross_check import runtime_inventory
  runtime=out/'runtime-libraries';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf')
  if not versions<=set(available):raise ValueError('target rootfs version closure missing '+repr(sorted(versions-set(available))))
  rec['target_runtime']=dict(base_rootfs=str(env['rootfs']),rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions),scope='MATCHED_LIBRARY_INPUT_CLOSURE_NOT_A_NEW_ROOTFS_IMAGE')
  client=root/'tools/vxgpuctl.py';py_compile.compile(str(client),cfile=str(out/'vxgpuctl.host-syntax-only.pyc'),doraise=True);shutil.copyfile(str(client),str(out/'vxgpuctl.py'))
  rec['installation']=[dict(source=str(out/'vxgpud'),destination='/usr/bin/vxgpud',sha256=sha(out/'vxgpud'),mode='0755'),dict(source=str(out/'vxgpuctl.py'),destination='/usr/bin/vxgpuctl',sha256=sha(out/'vxgpuctl.py'),mode='0755',interpreter='python3',bytecode='HOST_SYNTAX_CHECK_ONLY_DO_NOT_INSTALL_PYC')]
  rec['source_after']=snapshot(root)
  if rec['source_after']!=before:raise ValueError('source changed during matching AArch64 build')
  rec.update(natural_exit=0,elf_count=len(TARGETS))
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',default='.');p.add_argument('--env',required=True);p.add_argument('--source-lock',required=True);p.add_argument('--source-lock-sha',required=True);p.add_argument('--target',required=True,choices=['aarch64-s8','aarch64-p8','aarch64-s8-joint']);p.add_argument('--out',required=True);a=p.parse_args();r=build_target(a.env,a.source_lock,a.source_lock_sha,a.root,a.target,a.out);print(json.dumps(dict(scope=r['scope'],target=r['target'],natural_exit=r['natural_exit'],elf_count=r['elf_count'],executed_target_elfs=False,gpu_raw_cycles=None)))
