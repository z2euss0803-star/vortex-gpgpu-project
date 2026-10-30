"""Actual SDK 9.2 AArch64 and Linux5.4 compiler runs; no board or media access."""
import argparse,importlib.util,json,pathlib,re,shutil,struct,subprocess,sys,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import captured,compiler_identity,elf_record,native_output,readelf,sha,write_json
def package():
 s=importlib.util.spec_from_file_location('cross_fused_package',ROOT/'scripts/hunet_fused/package_release.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def checked_cpu(root,receipt,require_audit=True,packaged=False):
 if receipt.get('error') or (require_audit and receipt.get('audit',{}).get('integrity')!='PASS'):raise ValueError('FAILED_OR_UNAUDITED_CPU_RECEIPT')
 if receipt.get('scope')!='FUSED_CPU_AARCH64_BUILD' or receipt.get('exit')!=0 or receipt.get('compiler',{}).get('target')!='aarch64-xilinx-linux' or not re.search(r'\b9\.2\.0\b',receipt.get('compiler',{}).get('version','')):raise ValueError('ACTUAL_CPU_BUILD_REQUIRED')
 for name in ('vxl_hunet_fused','libvortex-fused.so'):
  p=root/('gelu.elf' if name=='vxl_hunet_fused' else name) if packaged else root/'objects'/name;b=p.read_bytes();record=receipt['elfs'][name]
  if b[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',b,18)[0]!=183 or sha(p)!=record['sha256'] or record['machine']!='AArch64':raise ValueError('ACTUAL_AARCH64_ELF_REQUIRED')
 if not (root/'cli.map').is_file() or not (root/'build.log.exit').is_file() or (root/'build.log.exit').read_text().strip()!='0':raise ValueError('NATURAL_CPU_BUILD_RECEIPT')
 return receipt
def audit_cross_files(root,receipt,binding,require_audit=True):
 root=pathlib.Path(root)
 if receipt.get('error') or (require_audit and receipt.get('audit',{}).get('integrity')!='PASS'):raise ValueError('FAILED_OR_UNAUDITED_KERNEL_RECEIPT')
 if receipt.get('scope')!='FUSED_KERNEL_AARCH64_BUILD' or receipt.get('exit')!=0 or receipt.get('W')!=1 or receipt.get('Werror') is not True or not receipt.get('kernel_release','').startswith('5.4.'):raise ValueError('ACTUAL_KERNEL_BUILD_REQUIRED')
 p=root/'source/driver/vxl_linux.ko';b=p.read_bytes()
 if len(binding)!=512 or binding not in b or sha(p)!=receipt.get('module_sha256') or b[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',b,18)[0]!=183:raise ValueError('BOUND_ACTUAL_KERNEL_MODULE')
 required=['shared_hunet_fused_'+n+'.o' for n in ('validate','state','evidence','program')]
 if receipt.get('fused_objects')!=required or any(not (p.parent/n).is_file() for n in required):raise ValueError('FUSED_KERNEL_OBJECTS')
 for name in ('vxl_fused_init','vxl_fused_check_program','vxl_fused_gpu_completed','vxl_fused_owner_trace'):
  if name not in receipt.get('fused_symbols',[]):raise ValueError('FUSED_KERNEL_SYMBOLS')
 if not (root/'build.log.exit').is_file() or (root/'build.log.exit').read_text().strip()!='0':raise ValueError('NATURAL_KERNEL_EXIT')
 return dict(integrity='PASS',scope='FUSED_KERNEL_CROSS_NOT_IMAGE_NOT_BOARD')
def build_cpu(source,identity,out,prefix,rootfs):
 pkg=package();source=pathlib.Path(source).resolve();identity=json.loads(pathlib.Path(identity).read_bytes());pkg.verify_source(source,identity)
 out=native_output(out);r=dict(scope='FUSED_CPU_AARCH64_BUILD',exit=None,source_commit=identity['source_commit'],source_digest=pkg.sha(pkg.canonical(identity)),board='NOT_RUN',sd='NOT_TOUCHED')
 try:
  compiler=compiler_identity(prefix);r['compiler']=compiler
  if compiler['target']!='aarch64-xilinx-linux' or not re.search(r'\b9\.2\.0\b',compiler['version']):raise ValueError('SDK_2020_1_REQUIRED')
  objects=out/'objects';command=['make','-C',str(source),'OUT='+str(objects),'CC='+prefix+'gcc','CXX='+prefix+'g++','LDFLAGS=-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1,-Map,'+str(out/'cli.map'),str(objects/'vxl_hunet_fused'),str(objects/'libvortex-fused.so')]
  r['command']=command;captured(command,out/'build.log');r['exit']=0;r['elfs']={};needed=set()
  for name in ('vxl_hunet_fused','libvortex-fused.so'):
   record=elf_record(prefix+'readelf',objects/name,r,bound=False);r['elfs'][name]=record;needed.update(record['needed'])
   (out/(name+'.readelf')).write_text(readelf(prefix+'readelf',objects/name,'-hW','-lW','-dW','-VW'))
  (out/'runtime').mkdir();from cross_check import runtime_inventory
  libs,versions=runtime_inventory(rootfs,needed,out/'runtime',prefix+'readelf');r['runtime_files']=libs;r['runtime_versions']=versions;r['rootfs_sha256']=sha(rootfs)
  required={v for e in r['elfs'].values() for v in e['required_versions']}
  if not required<=set(versions):raise ValueError('ROOTFS_SYMBOL_VERSION_MISMATCH')
  version=subprocess.check_output([prefix+'g++','-dM','-E','-x','c++','/dev/null'],universal_newlines=True)
  version=re.search(r'^#define __VERSION__ "([^"]+)"$',version,re.M).group(1)
  r['cpu_runtime']=dict(compiler=version,algorithm='ERF_NATIVE_V1',threads=4,precision_flags=['-fno-fast-math','-ffp-contract=off'],libm_bytes=(out/'runtime/libm.so.6').stat().st_size,libm_sha256=sha(out/'runtime/libm.so.6'))
  checked_cpu(out,r,require_audit=False);pkg.verify_source(source,identity)
  r['audit']=dict(integrity='PASS',scope='FUSED_CPU_CROSS_NOT_BOARD')
 except Exception as e:r['error']=str(e)
 write_json(out/'receipt.json',r)
 if r.get('error'):raise ValueError(r['error'])
 print('FUSED_CPU_AARCH64_CROSS_PASS_NOT_BOARD',out,flush=True);return r
def snapshot(release,dest):
 pkg=package();checked=pkg.verify_fused_release(release)
 if checked['integrity']!='PASS':raise ValueError(checked)
 release=pathlib.Path(release);dest=pathlib.Path(dest);dest.mkdir(exist_ok=False)
 with tarfile.open(release/'source.tar.gz','r:gz') as tar:
  for t in tar:
   pkg.legacy.safe_name(t.name)
   if not t.isfile():raise ValueError('SOURCE_NOT_REGULAR')
   p=dest/t.name;p.parent.mkdir(parents=True,exist_ok=True)
   with p.open('xb') as f:f.write(tar.extractfile(t).read())
 pkg.verify_source(dest,json.loads((release/'SOURCE_SHA256.json').read_bytes()))
 for name,source in [('vxl_hunet_source.h',release/'base/build/vxl_hunet_source.h'),('vxl_hybrid_release.h',release/'build/vxl_hybrid_release.h'),('vxl_fused_release.h',release/'build/vxl_fused_release.h')]:shutil.copyfile(source,dest/'driver'/name)
 return dest
def cross_release(release,toolchain,kernel,out,kernel_build=None):
 release=pathlib.Path(release).resolve();pkg=package();verified=pkg.verify_fused_release(release)
 if verified['integrity']!='PASS':raise ValueError(verified)
 r=dict(scope='FUSED_KERNEL_AARCH64_BUILD',exit=None,source_commit=verified['source_commit'],source_digest=verified['source_digest'],binding_sha256=verified['binding_sha256'],W=1,Werror=True,board='NOT_RUN',sd='NOT_TOUCHED')
 out=native_output(out);kernel=pathlib.Path(kernel).resolve();kb=pathlib.Path(kernel_build).resolve() if kernel_build else kernel
 try:
  r['compiler']=compiler_identity(toolchain)
  if r['compiler']['target']!='aarch64-xilinx-linux' or not re.search(r'\b9\.2\.0\b',r['compiler']['version']):raise ValueError('SDK_2020_1_REQUIRED')
  source=snapshot(release,out/'source');r['kernel_release']=(kb/'include/config/kernel.release').read_text().strip();r['config_sha256']=sha(kb/'.config');r['symvers_sha256']=sha(kb/'Module.symvers')
  if not r['kernel_release'].startswith('5.4.'):raise ValueError('MATCHING_KERNEL_REQUIRED')
  command=['make','-C',str(kernel),'O='+str(kb),'ARCH=arm64','CROSS_COMPILE='+toolchain,'M='+str(source/'driver'),'W=1','modules'];r['command']=command;captured(command,out/'build.log');r['exit']=0
  module=source/'driver/vxl_linux.ko';r['module_sha256']=sha(module);symbols=subprocess.check_output([toolchain+'nm','--defined-only',str(module)],universal_newlines=True);(out/'module.nm').write_text(symbols)
  (out/'module.readelf').write_text(readelf(toolchain+'readelf',module,'-hW','-p','.modinfo'));r['fused_symbols']=[n for n in ('vxl_fused_init','vxl_fused_check_program','vxl_fused_gpu_completed','vxl_fused_owner_trace') if re.search(r'\b'+n+r'\s*$',symbols,re.M)]
  r['fused_objects']=['shared_hunet_fused_'+n+'.o' for n in ('validate','state','evidence','program') if (source/'driver'/('shared_hunet_fused_'+n+'.o')).is_file()]
  r['audit']=audit_cross_files(out,r,(release/'binding-fused.bin').read_bytes(),require_audit=False)
  if sha(kb/'.config')!=r['config_sha256'] or sha(kb/'Module.symvers')!=r['symvers_sha256']:raise ValueError('KERNEL_INPUT_CHANGED')
 except Exception as e:r.update(error=str(e),audit=dict(integrity='FAIL',reason=str(e)))
 write_json(out/'receipt.json',r)
 if r['audit']['integrity']!='PASS':raise ValueError(r['audit'])
 print('FUSED_KERNEL_CROSS_AUDIT_PASS_NOT_IMAGE_NOT_BOARD',out,flush=True);return r
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=('cpu','kernel'));p.add_argument('--source',type=pathlib.Path);p.add_argument('--identity',type=pathlib.Path);p.add_argument('--release',type=pathlib.Path);p.add_argument('--toolchain',required=True);p.add_argument('--kernel',type=pathlib.Path);p.add_argument('--kernel-build',type=pathlib.Path);p.add_argument('--rootfs',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
 build_cpu(a.source,a.identity,a.out,a.toolchain,a.rootfs) if a.kind=='cpu' else cross_release(a.release,a.toolchain,a.kernel,a.out,a.kernel_build)
