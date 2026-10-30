from scripts.unified.hunet_program import selected_directory as selected_hunet_directory
"""Actual sealed application builder and daemon, HOST OS/protocol oracle only."""
import pathlib,subprocess,sys,time,json,os,struct,re,tempfile,shutil,stat
from scripts.unified.mosaic_program import selected_directory
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture

def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','p8-red','all','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 record=dict(scope='INSTALLED_SEALED_ENTRY_AND_ACTUAL_DAEMON_HOST_NOT_GPU_PHYSICAL_DP',commands=[],gpu_raw_cycles=None)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(record['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 600 if pathlib.Path(argv[0]).name=='test_installed_p8' else 180)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)))
  assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','INSTALLATION_ROLLBACK_','NATIVE_FILES_SEAL_ERROR'))
  record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])))
  (out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(text[-2200:],flush=True)
  return r.returncode,text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  complete=mode in ('all','sanitize','thread','cross')
  env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text())
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if complete and mode!='cross':
   from scripts.unified.generate_mosaic_package import generate
   weights=pathlib.Path(env['mosaic_assets']);small=out/'input-small.f32';small.write_bytes(b''.join(struct.pack('<f',(i%37-18)*.025) for i in range(288)))
   model=out/'model-small';record['mosaic_package']=generate(weights,weights.parent/'manifest.json',small,12,8,64,model)
   for p in model.iterdir():p.chmod(0o400)
   model.chmod(0o500);hunet=out/'hunet-model';weights=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin')
   assert sha(weights)=='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc'
   code,text=run([sys.executable,'-m','scripts.unified.generate_hunet_package',weights,hunet]);assert code==0;record['hunet_package']=json.loads(text)
   for p in hunet.iterdir():p.chmod(0o400)
   hunet.chmod(0o500)
  targets=['test_installed_p8'] if mode=='p8-red' else ['test_installed_image']+(['test_installed_p8','vxgpud','test_native_daemon','test_native_daemon_hunet','test_daemon','test_preparation','test_installed_media','test_native_result_view','test_native_result_export','test_native_export_bridge','test_texture_source'] if complete else [])
  if mode=='cross':
   prefix=env['aarch64_prefix'];code,version=run([prefix+'g++','--version']);assert code==0 and '9.2.0' in version;code,machine=run([prefix+'gcc','-dumpmachine']);assert code==0 and machine.strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256']
   drm_path=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T19-active-encoder-cross-20261019/cross/build-receipt.json');drm=json.loads(drm_path.read_text());drm_prefix=pathlib.Path(env['drm_target']);drm_lib=drm_prefix/'lib/libdrm.a'
   assert sha(drm_path)=='4aff06bdf86e9e5a704a0e6da65d4991eabccbb60c7d7d49f20eb38b0bffa69a' and drm['natural_exit']==0 and sha(drm_lib)==drm['dependencies']['drm-target/lib/libdrm.a']['sha256']==env['drm_archive_sha256']
   record.update(json_dependency=dict(receipt_sha256=sha(dep_path),library=lib),drm_dependency=dict(receipt_sha256=sha(drm_path),library=artifact(drm_lib)))
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent),'U18_DRM_PREFIX='+str(drm_prefix)]
  code,text=run(cmd+[out/t for t in targets]+['-j'+str(jobs)]);assert code==0,'compile is not functional RED'
  if mode=='cross':
   needed=set();versions=set()
   for target in targets:
    binary=out/target;raw=binary.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    code,text=run([prefix+'readelf','-hW','-dW','-VW',binary]);assert code==0;needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available)
   record.update(natural_exit=0,scope='MATCHED_AARCH64_NATIVE_MEDIA_NN_VIEW_AND_EXPORT_ELFS_NOT_EXECUTED',elf_count=len(targets),executed_target_elfs=False,target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)));return
  if mode!='p8-red':
   code,text=run([out/'test_installed_image','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/'image-output'])
   assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
   if mode=='red':
    assert code==1 and 'U20 sealed IMAGE installation must bind exactly one shared native display and UDP service' in text
    record['functional_red']='TYPED_INSTALLATION_ACCEPTED_BUT_NATIVE_IMAGE_DISPLAY_INGRESS_NOT_BOUND'
   else:
    assert code==0 and 'U20_INSTALLED_IMAGE_ACTUAL_SEALED_STARTUP_UDP_FULL720_COMMON_FILES_DISPLAY_AND_SHUTDOWN_PASS_HOST_NOT_GPU_DP' in text
    code,_=run([out/'test_installed_image','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/'partial-output','partial']);assert code==0
  if mode=='p8-red' or complete:
   code,text=run([out/'test_installed_p8','data/p8/programs/catalog.bin','data/p8/programs','data/p8',out/'p8-output'])
   if mode=='p8-red':
    assert code==1 and 'U20 sealed P8 installation must bind native P8 without an S8 NN library' in text
    record['functional_red']='P8_INSTALLATION_MUST_NOT_CONSTRUCT_S8_NN_LIBRARY'
   else:assert code==0 and 'U20_INSTALLED_P8_SEALED_STARTUP100_COMMON_DAEMON_FILES_DISPLAY_AND_SHUTDOWN_PASS_HOST_NOT_GPU_DP' in text
  if complete:
   code,_=run(cmd+[out/'mosaic-owner-catalog.bin',out/'hunet-owner-catalog.bin',out/'catalog-fixture.bin','-j1']);assert code==0
   for binary,package,catalog,program,ro,tag in [('test_native_daemon',model,out/'mosaic-owner-catalog.bin',selected_directory('.')/'code.bin',selected_directory('.')/'rodata.bin','mosaic'),('test_native_daemon_hunet',hunet,out/'hunet-owner-catalog.bin',selected_hunet_directory('.')/'code.bin','unused','hunet')]:
    code,_=run([out/binary,package,catalog,program,ro,out/(tag+'-v1'),'normal']);assert code==0
   code,_=run([out/'test_installed_media']);assert code==0
   code,_=run([out/'test_preparation']);assert code==0
   for case in ('lifecycle','lifecycle-activate','lifecycle-quiesce','lifecycle-display','lifecycle-resources','shutdown-unreaped','shutdown-held','slow-preparation','close-preparation','identity-preparation','error-preparation','throw-preparation','deferred','legacy','fault'):
    code,_=run([out/'test_daemon',out/'catalog-fixture.bin',out/('journal-'+case),case]);assert code==0
   code,_=run(cmd+['test-unified-daemon-cli','-j1']);assert code==0
   code,_=run([out/'test_daemon',out/'catalog-fixture.bin',out/'startup-error-journal','startup-journal']);assert code==0
   from scripts.unified.prepare_hunet_view import prepare
   view=pathlib.Path(tempfile.mkdtemp(prefix='vx-native-view-assets-'))/'view';record['view_package']=prepare('data/demo',view);snapshot=out/'view';snapshot.mkdir(mode=0o700)
   for original in view.iterdir():
    p=snapshot/original.name;shutil.copyfile(str(original),str(p));p.chmod(stat.S_IMODE(original.stat().st_mode));assert sha(p)==sha(original)
   code,_=run([out/'test_native_result_view',view,record['view_package']['sha256']]);assert code==0
   code,_=run([out/'test_native_daemon_hunet',hunet,out/'hunet-owner-catalog.bin',selected_hunet_directory('.')/'code.bin','unused',out/'hunet-installed-view','view',view,record['view_package']['sha256']]);assert code==0
   code,text=run([out/'test_native_daemon',model,out/'mosaic-owner-catalog.bin',selected_directory('.')/'code.bin',selected_directory('.')/'rodata.bin',out/'mosaic-installed-export','export']);assert code==0
   source=next(x.split('=',1)[1] for x in text.splitlines() if x.startswith('NATIVE_INSTALLED_EXPORTED_MANIFEST='))
   from scripts.unified.texture_package import build_package
   texture=out/'installed-texture';meta=build_package(source,texture);record['installed_texture_id']=meta['package_id'];code,_=run([out/'test_native_export_bridge',source,texture,meta['package_id'],'installed']);assert code==0
   code,text=run([out/'test_native_result_export']);assert code==0;source=next(x.split('=',1)[1] for x in text.splitlines() if x.startswith('NATIVE_EXPORTED_MANIFEST='));texture=out/'retained-texture';meta=build_package(source,texture);record['retained_texture_id']=meta['package_id'];code,_=run([out/'test_native_export_bridge',source,texture,meta['package_id']]);assert code==0
   code,_=run([out/'test_texture_source']);assert code==0
   code,_=run(['python3','-m','unittest','tests.unified.test_texture_package','tests.unified.test_hunet_view_package']);assert code==0
  record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
