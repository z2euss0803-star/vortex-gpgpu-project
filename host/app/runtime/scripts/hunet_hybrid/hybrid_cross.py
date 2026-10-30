"""Actual GCC9.2 user/kernel builds from a verified new release."""
import pathlib,re,sys
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import captured,compiler_identity,elf_record,native_output,readelf,sha,write_json
from cross_audit import ARTIFACTS,HYBRID_OBJECTS,HYBRID_SYMBOLS,audit_hybrid_cross,audit_hybrid_kernel
from hybrid_build import checked_hybrid,hybrid_snapshot
def build_cross(release_dir,out_dir,prefix,rootfs):
 from cross_check import runtime_inventory
 manifest=checked_hybrid(release_dir);out=native_output(out_dir);r=dict(scope='HUNET_HYBRID_AARCH64_CROSS',exit=None,source_commit=manifest['source_commit'],source_digest=manifest['source_digest'],binding_sha256=manifest['binding_sha256'],elfs={},rootfs_libraries=[],runtime_versions=[],rootfs_tar_sha256=sha(rootfs),board='NOT_RUN',sd='NOT_TOUCHED')
 try:
  r['compiler']=compiler_identity(prefix)
  if r['compiler']['target']!='aarch64-xilinx-linux' or not re.search(r'\b9\.2\.0\b',r['compiler']['version']):raise ValueError('MATCHING_2020_1_COMPILER_REQUIRED')
  source=hybrid_snapshot(release_dir,out/'source');r['verified_source_snapshot']=True
  command=['make','-C',str(source),'userspace-cross','hybrid-userspace-cross','CROSS_COMPILE='+prefix,'SOURCE_COMMIT='+manifest['source_commit'],'HUNET_BINDING_HEADER='+str(source/'build/vxl_hunet_source.h')];r['command']=command;captured(command,out/'build.log');r['exit']=0
  needed=set()
  for name in ARTIFACTS+('libvortex-hybrid.so','vxl_hunet_hybrid'):
   cpu=name=='vxl_hunet_hybrid';value=elf_record(prefix+'readelf',source/'build/aarch64'/name,manifest,bound=not cpu)
   if cpu:value['binding_kind']='RELEASE_SHA_AND_VERIFIED_SOURCE_SNAPSHOT'
   r['elfs'][name]=value;needed.update(value['needed'])
  (out/'runtime').mkdir();libs,versions=runtime_inventory(rootfs,needed,out/'runtime',prefix+'readelf');r['rootfs_libraries']=sorted(libs);r['runtime_versions']=versions;r['runtime_files']=libs
 except Exception as e:r['error']=str(e)
 r['audit']=audit_hybrid_cross(r,manifest);write_json(out/'receipt.json',r)
 if r['audit']['integrity']!='PASS':raise ValueError(r['audit'])
 print('HYBRID_AARCH64_CROSS_PASS_NOT_BOARD',out,flush=True);return r
def build_kernel(release_dir,out_dir,prefix,kernel_source,kernel_build,actual=False):
 manifest=checked_hybrid(release_dir);out=native_output(out_dir);kernel_source=pathlib.Path(kernel_source).resolve();kernel_build=pathlib.Path(kernel_build).resolve()
 r=dict(scope='ACTUAL_IMAGE_KERNEL_CROSS' if actual else 'PRELIMINARY_KERNEL_CROSS',exit=None,source_commit=manifest['source_commit'],source_digest=manifest['source_digest'],source_tree=str(kernel_source),kernel_build=str(kernel_build),W=1,Werror=True,board='NOT_RUN',sd='NOT_TOUCHED')
 try:
  comp=compiler_identity(prefix);r['toolchain']=comp;r['compiler']=comp['version'];config=kernel_build/'.config';sym=kernel_build/'Module.symvers';r['config']=config.read_text().splitlines();r['config_sha256']=sha(config);r['symvers_sha256']=sha(sym);r['kernel_release']=(kernel_build/'include/config/kernel.release').read_text().strip()
  if not r['kernel_release'].startswith('5.4.'):raise ValueError('MATCHING_KERNEL_REQUIRED')
  source=hybrid_snapshot(release_dir,out/'source');command=['make','-C',str(kernel_source),'O='+str(kernel_build),'ARCH=arm64','CROSS_COMPILE='+prefix,'M='+str(source/'driver'),'W=1','modules'];r['command']=command;captured(command,out/'build.log');r['exit']=0
  module=source/'driver/vxl_linux.ko';value=elf_record(prefix+'readelf',module,manifest);r['arch']=value['machine'];r['module_sha256']=sha(module);r['source_bound']=value['source_commit']==manifest['source_commit'] and value['source_digest']==manifest['source_digest'];info=readelf(prefix+'readelf',module,'-p','.modinfo');match=re.search(r'vermagic=([^\n]+)',info);r['vermagic']=match.group(1).strip() if match else ''
  canonical=(pathlib.Path(release_dir)/'binding-hybrid.bin').read_bytes();r['hybrid_binding_sha256']=sha(pathlib.Path(release_dir)/'binding-hybrid.bin') if canonical in module.read_bytes() else None
  r['hybrid_objects']=[name for name in HYBRID_OBJECTS if (source/'driver'/name).is_file()];symbols=__import__('subprocess').check_output([prefix+'nm','--defined-only',str(module)],universal_newlines=True);r['hybrid_symbols']=[name for name in HYBRID_SYMBOLS if re.search(r'\b'+name+r'\s*$',symbols,re.M)]
  if sha(config)!=r['config_sha256'] or sha(sym)!=r['symvers_sha256']:raise ValueError('KERNEL_INPUT_CHANGED')
 except Exception as e:r['error']=str(e)
 r['audit']=audit_hybrid_kernel(r,manifest);write_json(out/'receipt.json',r)
 if r['audit']['integrity']!='PASS':raise ValueError(r['audit'])
 print(r['scope']+'_PASS_NOT_BOARD',out,flush=True);return r
