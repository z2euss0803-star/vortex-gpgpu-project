"""Matching AArch64 userspace, ELF/runtime validation. Never executes the ELF."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'hunet'))
from scripts.hunet.build_common import compiler_identity,elf_record,readelf,sha
from scripts.hunet.cross_check import runtime_inventory
from scripts.s8.build_rv32 import run
from scripts.s8.freeze import source_digest
def validate_elf(raw):
 if len(raw)<64 or raw[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',raw,18)[0]!=183:raise ValueError('actual ELF64 AArch64 required')
 return True
def build(env,root,out):
 env=json.loads(Path(env).read_text());root=Path(root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 record=dict(scope='ACTUAL_AARCH64_USERSPACE_NOT_BOARD',integrity='FAIL',gpu_raw_cycles=None,source_sha256=source_digest(root))
 try:
  prefix=env['aarch64_prefix'];compiler=compiler_identity(prefix);record['compiler']=compiler
  if compiler['target']!='aarch64-xilinx-linux' or '9.2.0' not in compiler['version']:raise ValueError('matching 2020.1 SDK required')
  sysroot=subprocess.check_output([prefix+'gcc','-print-sysroot']).decode().strip();record['sysroot']=sysroot
  if not sysroot or not Path(sysroot).is_dir():raise ValueError('missing actual SDK sysroot')
  objects=out/'objects';cmd=['make','-j1','-C',str(root),'OUT='+str(objects),'CC='+prefix+'gcc','CXX='+prefix+'g++','LDFLAGS=-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1','s8-cpu'];record['command']=cmd;run(cmd,out/'build.log')
  record['elfs']={};needed=set()
  for name in ('libvxl-s8.so','vxl_s8'):
   path=objects/name;validate_elf(path.read_bytes());item=elf_record(prefix+'readelf',path,{},bound=False)
   if name=='vxl_s8' and item['interpreter']!='/lib/ld-linux-aarch64.so.1':raise ValueError('wrong target loader')
   needed.update(item['needed']);record['elfs'][name]=item;(out/(name+'.readelf')).write_text(readelf(prefix+'readelf',path,'-hW','-lW','-dW','-VW'))
  needed.discard('libvxl-s8.so');(out/'runtime').mkdir();libs,versions=runtime_inventory(Path(env['rootfs']),needed,out/'runtime',prefix+'readelf')
  required={v for row in record['elfs'].values() for v in row['required_versions']}
  if not required<=set(versions):raise ValueError('target rootfs symbol versions unavailable')
  symbols=subprocess.check_output([prefix+'nm','-D','--defined-only',str(objects/'libvxl-s8.so')]).decode();(out/'library.nm').write_text(symbols)
  for name in ('vxl_s8_run_case','vxl_s8_client_bind','vxl_s8_client_prepare'):
   if not re.search(r'\b'+name+r'$',symbols,re.M):raise ValueError('missing actual exported client '+name)
  if source_digest(root)!=record['source_sha256']:raise ValueError('source changed during cross build')
  record.update(integrity='PASS',exit=0,runtime_files=libs,runtime_versions=versions,rootfs_sha256=sha(env['rootfs']))
 except Exception as exc:record.update(error=str(exc),exit=1)
 (out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
 if record['exit']:raise ValueError(record['error'])
 return record
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('.'));p.add_argument('--env',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();r=build(a.env,a.root,a.out);print(json.dumps(dict(scope=r['scope'],integrity=r['integrity'],exit=r['exit'])))
