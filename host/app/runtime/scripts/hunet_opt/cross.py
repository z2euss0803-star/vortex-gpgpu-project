"""Actual matching AArch64 compilation. No target execution or device access."""
import argparse,hashlib,json,pathlib,re,struct,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT));sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import captured,compiler_identity,elf_record,sha,write_json,readelf,native_output
def elf_machine(raw,elf_class,machine):
 if len(raw)<64 or raw[:6]!=b'\x7fELF'+bytes((elf_class,1)) or struct.unpack_from('<H',raw,18)[0]!=machine:raise ValueError('ELF_MACHINE_IDENTITY')
 return 'PASS'
def audit_sha2_isolation(objects):
 pattern=r'\bsha256(?:h2?|su[01])\b'
 if not re.search(pattern,objects.get('sha2','')):raise ValueError('SHA2_OBJECT_WITHOUT_SHA256_INSTRUCTIONS')
 for name,text in objects.items():
  if name!='sha2' and re.search(pattern,text):raise ValueError('SHA2_BASELINE_LEAK:'+name)
 return dict(integrity='PASS',scope='STATIC_ISA_ISOLATION_NOT_A53_EXECUTION')
def build_cpu(source,catalog,env,out,identity=None):
 source=pathlib.Path(source).resolve();catalog=pathlib.Path(catalog).resolve();out=native_output(out)
 c=json.loads(catalog.read_bytes());prefix=env['aarch64_prefix'];r=dict(scope='OPT_CPU_AARCH64_BUILD',exit=None,candidate=c['candidate'],board='NOT_RUN',sd='NOT_TOUCHED',source_identity=identity)
 try:
  from scripts.hunet_opt.build_identity import verify_snapshot
  r.update(verify_snapshot(source,identity or env['source_identity']))
  r['compiler']=compiler_identity(prefix)
  if r['compiler']['target']!='aarch64-xilinx-linux' or not re.search(r'\b9\.2\.0\b',r['compiler']['version']):raise ValueError('SDK_2020_1_REQUIRED')
  objects=out/'objects';cpp='-Iinclude -Ibuild/generated -Ivendor/vortex/runtime/include -Ivendor/vortex/runtime/common -Ivendor/vortex/sim/common -DVXL_OPT_CATALOG_HEADER=\\"../generated/hunet_opt/'+c['candidate']+'/catalog.h\\"'
  command=['make','-j2','-C',str(source),'OUT='+str(objects),'OPT_CANDIDATE='+c['candidate'],'OPT_CRYPTO=1','CPPFLAGS='+cpp,'CC='+prefix+'gcc','CXX='+prefix+'g++','LDFLAGS=-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1,-Map,'+str(out/'cli.map'),str(objects/'vxl_hunet_opt'),str(objects/'libvortex-opt.so')]
  r['command']=command;captured(command,out/'build.log');r['exit']=0;r['elfs']={};needed=set()
  for name in ('vxl_hunet_opt','libvortex-opt.so'):
   p=objects/name;elf_machine(p.read_bytes(),2,183);record=elf_record(prefix+'readelf',p,r,bound=False);r['elfs'][name]=record;needed.update(record['needed'])
   (out/(name+'.readelf')).write_text(readelf(prefix+'readelf',p,'-hW','-lW','-dW','-VW'))
  disasm={}
  for name,p in [('dispatch',objects/'userspace/digest_dispatch.o'),('sha2',objects/'userspace/digest_sha2_block.o'),('wrapper',objects/'userspace/digest_sha2.o'),('portable',objects/'core/hunet_sha256.o')]:
   disasm[name]=subprocess.check_output([prefix+'objdump','-d',str(p)],universal_newlines=True);(out/(name+'.disasm')).write_text(disasm[name])
  # Only digest_sha2_block.o is compiled with +crypto.
  r['sha2_isolation']=audit_sha2_isolation(disasm)
  from cross_check import runtime_inventory
  (out/'runtime').mkdir();libs,versions=runtime_inventory(pathlib.Path(env['rootfs']),needed,out/'runtime',prefix+'readelf')
  r['runtime_files']=libs;r['runtime_versions']=versions;r['rootfs_sha256']=sha(pathlib.Path(env['rootfs']))
  required={v for x in r['elfs'].values() for v in x['required_versions']}
  if not required<=set(versions):raise ValueError('ROOTFS_SYMBOL_VERSION_MISMATCH')
  macros=subprocess.check_output([prefix+'g++','-dM','-E','-x','c++','/dev/null'],universal_newlines=True)
  compiler=re.search(r'^#define __VERSION__ "([^"]+)"$',macros,re.M).group(1)
  r['cpu_runtime']=dict(compiler=compiler,algorithm='ERF_NATIVE_V1',threads=4,precision_flags=['-fno-fast-math','-ffp-contract=off'],libm_bytes=(out/'runtime/libm.so.6').stat().st_size,libm_sha256=sha(out/'runtime/libm.so.6'))
  r['audit']=dict(integrity='PASS',scope='CROSS_COMPILE_NOT_BOARD')
 except Exception as e:r.update(error=str(e),audit=dict(integrity='FAIL',reason=str(e)))
 write_json(out/'receipt.json',r)
 if r.get('error'):raise ValueError(r['error'])
 return r
def build_kernel(source,env,out,release_headers=None,identity=None,candidate='COMBINED'):
 import shutil
 out=native_output(out);source=pathlib.Path(source).resolve();r=dict(scope='OPT_KERNEL_AARCH64_BUILD',exit=None,source_identity=identity,board='NOT_RUN',W=1,Werror=True)
 try:
  from scripts.hunet_opt.build_identity import verify_snapshot
  r.update(verify_snapshot(source,identity or env['source_identity']))
  private=out/'source';private.mkdir()
  for name in ('driver','core','include','generated'):
   shutil.copytree(source/name,private/name,ignore=shutil.ignore_patterns('__pycache__','*.pyc','*.o','*.ko','*.cmd','Module.symvers','modules.order'))
  if release_headers:
   for p in pathlib.Path(release_headers).glob('*.h'):shutil.copyfile(p,private/'driver'/p.name)
  prefix=env['aarch64_prefix'];kernel=pathlib.Path(env['kernel_source']);kb=pathlib.Path(env['kernel_build'])
  r.update(compiler=compiler_identity(prefix),kernel_release=(kb/'include/config/kernel.release').read_text().strip(),config_sha256=sha(kb/'.config'),symvers_sha256=sha(kb/'Module.symvers'))
  if not r['kernel_release'].startswith('5.4.'):raise ValueError('KERNEL_5_4_REQUIRED')
  command=['make','-j2','-C',str(kernel),'O='+str(kb),'ARCH=arm64','CROSS_COMPILE='+prefix,'M='+str(private/'driver'),'W=1','KCFLAGS=-DVXL_OPT_CATALOG_HEADER=\\"../generated/hunet_opt/'+candidate+'/catalog.h\\"','modules']
  r['command']=command;captured(command,out/'build.log');r['exit']=0
  p=private/'driver/vxl_linux.ko';elf_machine(p.read_bytes(),2,183);r['module_sha256']=sha(p)
  symbols=subprocess.check_output([prefix+'nm','--defined-only',str(p)],universal_newlines=True);(out/'module.nm').write_text(symbols)
  for name in ('vxl_opt_init','vxl_opt_prepare_control','vxl_opt_gpu_completed','vxl_kernel_crypto_ops'):
   if not re.search(r'\b'+name+r'\s*$',symbols,re.M):raise ValueError('OPT_KERNEL_SYMBOL:'+name)
  r['opt_objects']=sorted(p.name for p in (private/'driver').glob('shared_hunet_opt_*.o'))
  if sha(kb/'.config')!=r['config_sha256'] or sha(kb/'Module.symvers')!=r['symvers_sha256']:raise ValueError('KERNEL_INPUT_MUTATION')
  r['audit']=dict(integrity='PASS',scope='KERNEL_MODULE_COMPILE_NOT_BUILTIN_IMAGE_NOT_BOARD')
 except Exception as e:r.update(error=str(e),audit=dict(integrity='FAIL',reason=str(e)))
 write_json(out/'receipt.json',r)
 if r.get('error'):raise ValueError(r['error'])
 return r
def cross_build(source,catalog,env,out):
 out=pathlib.Path(out);out.mkdir(exist_ok=False);cpu=build_cpu(source,catalog,env,out/'cpu');kernel=build_kernel(source,env,out/'kernel',candidate=json.loads(pathlib.Path(catalog).read_bytes())['candidate'])
 return dict(cpu=cpu,kernel=kernel)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=('cpu','kernel'));p.add_argument('--source',type=pathlib.Path,default=ROOT);p.add_argument('--catalog',type=pathlib.Path,required=True);p.add_argument('--env',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args();env=json.loads(a.env.read_bytes());print(json.dumps(build_cpu(a.source,a.catalog,env,a.out) if a.kind=='cpu' else build_kernel(a.source,env,a.out,candidate=json.loads(a.catalog.read_bytes())['candidate'])))
