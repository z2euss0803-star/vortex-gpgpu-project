"""Actual Linux5.4 headers/adapter cross build. Pre-binding never deploys."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'hunet'))
from scripts.hunet.build_common import compiler_identity,sha
from scripts.s8.build_cpu import validate_elf
from scripts.s8.build_rv32 import run
from scripts.s8.freeze import source_digest
def scope(binding):return 'PRE_BINDING_KERNEL_COMPILE_NOT_IMAGE' if binding is None else 'BOUND_KERNEL_MODULE_COMPILE_NOT_BUILTIN_IMAGE'
def validate_kernel(facts):
 if not facts.get('release','').startswith('5.4.') or not facts.get('symvers') or facts.get('headers') is not True:raise ValueError('missing matching Linux5.4 build inputs')
 if any(s not in facts.get('config','') for s in ('CONFIG_ARM64=y','CONFIG_DEBUG_FS=y','CONFIG_CRYPTO_SHA256=y')):raise ValueError('kernel config missing architecture/debugfs/crypto')
 return True
def build(env,root,out,binding=None):
 env=json.loads(Path(env).read_text());root=Path(root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 record=dict(scope=scope(binding),integrity='FAIL',gpu_raw_cycles=None,image='NOT_BUILT',board='NOT_RUN',source_sha256=source_digest(root))
 try:
  source=Path(env['kernel_source']);kb=Path(env['kernel_build']);prefix=env['aarch64_prefix'];record['compiler']=compiler_identity(prefix)
  facts=dict(release=(kb/'include/config/kernel.release').read_text().strip(),config=(kb/'.config').read_text(),symvers=(kb/'Module.symvers').read_bytes(),headers=(kb/'include/generated/autoconf.h').is_file() and (source/'include/linux/debugfs.h').is_file())
  validate_kernel(facts);record.update(kernel_release=facts['release'],config_sha256=sha(kb/'.config'),symvers_sha256=sha(kb/'Module.symvers'),kernel_source=str(source),kernel_build=str(kb))
  if record['compiler']['target']!='aarch64-xilinx-linux' or '9.2.0' not in record['compiler']['version']:raise ValueError('matching target SDK required')
  private=out/'source';private.mkdir()
  for name in ('driver','core','include','generated'):shutil.copytree(str(root/name),str(private/name),ignore=shutil.ignore_patterns('__pycache__','*.o','*.ko','*.cmd','Module.symvers','modules.order','vxl_s8_release.h'))
  if binding is not None:
   binding=Path(binding)
   if not (binding/'binding.bin').is_file() or len((binding/'binding.bin').read_bytes())!=512 or not (binding/'vxl_s8_release.h').is_file():raise ValueError('full final binding required')
   shutil.copyfile(str(binding/'vxl_s8_release.h'),str(private/'driver/vxl_s8_release.h'))
  cmd=['make','-j1','-C',str(source),'O='+str(kb),'ARCH=arm64','CROSS_COMPILE='+prefix,'M='+str(private/'driver'),'W=1','modules'];record['command']=cmd;run(cmd,out/'build.log')
  module=private/'driver/vxl_linux.ko';validate_elf(module.read_bytes());symbols=subprocess.check_output([prefix+'nm','--defined-only',str(module)]).decode();(out/'module.nm').write_text(symbols)
  for name in ('vxl_s8_attach','vxl_s8_trace_emit','vxl_s8_tracefs_init','vxl_s8_validate_job','vxl_s8_complete'):
   if not re.search(r'\b'+name+r'$',symbols,re.M):raise ValueError('missing S8 kernel path '+name)
  info=subprocess.check_output([prefix+'readelf','-p','.modinfo',str(module)]).decode();(out/'module.modinfo').write_text(info)
  match=re.search(r'vermagic=([^\n]+)',info)
  if not match or facts['release'] not in match.group(1):raise ValueError('wrong kernel vermagic')
  if sha(kb/'.config')!=record['config_sha256'] or sha(kb/'Module.symvers')!=record['symvers_sha256']:raise ValueError('shared kernel inputs changed')
  if binding is not None and (binding/'binding.bin').read_bytes() not in module.read_bytes():raise ValueError('full binding absent from actual linked module')
  if source_digest(root)!=record['source_sha256']:raise ValueError('source changed during kernel build')
  record.update(integrity='PASS',exit=0,module_sha256=sha(module),vermagic=match.group(1).strip(),profile5_ready='FORBIDDEN_PRE_BINDING' if binding is None else 'NOT_EXECUTED')
 except Exception as exc:record.update(error=str(exc),exit=1)
 (out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
 if record['exit']:raise ValueError(record['error'])
 return record
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('.'));p.add_argument('--env',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--binding',type=Path);a=p.parse_args();r=build(a.env,a.root,a.out,a.binding);print(json.dumps(dict(scope=r['scope'],integrity=r['integrity'],exit=r['exit'])))
