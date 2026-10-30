"""Compile full frozen SDK + controlled S8 program; no target execution."""
from pathlib import Path
import argparse,hashlib,json,shutil,struct,subprocess
from scripts.s8.audit_rv32 import audit,sections
from scripts.s8.freeze import source_digest
from scripts.hunet.audit_program import elf_info
from scripts.hunet_opt.build_rv32 import DEFINES,clang_resource_dir
SOURCES=tuple('device/s8/'+n+'.cpp' for n in ('main','admission','protocol','negative','compat','phase','unpack'))
SDK_SOURCES=('vx_start.S','vx_spawn.c','vx_syscalls.c')
FLAGS=['-march=rv32imaf','-mabi=ilp32f','-O2','-mcmodel=medany','-fno-fast-math','-ffp-contract=off','-fstack-usage','-fdata-sections','-ffunction-sections']
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd,path):
 with Path(path).open('xb') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 Path(str(path)+'.exit').write_text(str(p.returncode)+'\n')
 if p.returncode:raise ValueError('command exit '+str(p.returncode)+' '+str(path))
def build(root,env,out):
 root=Path(root).resolve();env=json.loads(Path(env).read_text());out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 receipt=dict(scope='FULL_RV32_CROSS_COMPILE_NOT_BOARD',integrity='FAIL',gpu_raw_cycles=None,commands=[],source_sha256=source_digest(root))
 try:
  tools=Path(env['rv32_tools']);gnu=tools/'riscv32-gnu-toolchain';clang=tools/'llvm-vortex/bin/clang++';gcc=gnu/'bin/riscv32-unknown-elf-gcc';ar=gnu/'bin/riscv32-unknown-elf-gcc-ar'
  sdk=root/'vendor/hunet/sdk-linux-crt';resource=clang_resource_dir(tools)
  version=subprocess.check_output([str(clang),'--version']).decode();receipt['clang_version']=version
  if '18.1.7' not in version:raise ValueError('requires frozen Clang18.1.7')
  flags=[f for f in DEFINES if not f.startswith(('-DIO_MPM_ADDR=','-DHUNET_SCALAR_CHUNK='))]+['-DIO_MPM_ADDR=0x10002000']
  includes=['-I'+str(p) for p in (root/'include',sdk/'kernel/include',sdk/'sim/common',sdk/'hw')]
  ccflags=FLAGS+flags+includes
  cxxflags=['--sysroot='+str(gnu/'riscv32-unknown-elf'),'--gcc-toolchain='+str(gnu),'-resource-dir='+str(resource),'-Xclang','-target-feature','-Xclang','+vortex','-Xclang','-target-feature','-Xclang','+zicond','-mllvm','-disable-loop-idiom-all','-std=c++17','-fno-exceptions','-fno-rtti']+ccflags
  receipt.update(compiler_sha256=digest(clang),tool_archive_sha256=env['tool_archive_sha256'],flags=cxxflags,sources={},dependencies={})
  archive=out/'libvortex.a';shutil.copyfile(env['vortex_archive'],archive)
  members=subprocess.check_output([str(ar),'t',str(archive)]).decode().splitlines()
  stale=[n for n in members if n.split('.')[0] in ('vx_start','vx_print','vx_perf','vx_spawn','vx_syscalls')]
  def execute(cmd,log):receipt['commands'].append([str(x) for x in cmd]);run([str(x) for x in cmd],out/log)
  if stale:execute([ar,'d',archive]+stale,'remove-old-sdk.log')
  objects=[];owners={}
  units=[(sdk/'kernel/src'/n,gcc,ccflags) for n in SDK_SOURCES]+[(root/'device/s8/crt/perf_stub.c',gcc,ccflags)]+[(root/n,clang,cxxflags) for n in SOURCES]
  for i,(source,compiler,compileflags) in enumerate(units):
   obj=out/('%02d-'%i+source.name+'.o');execute([compiler]+compileflags+['-MMD','-MF',str(obj)+'.d','-c',source,'-o',obj],'compile-'+source.name+'.log');objects.append(obj)
   receipt['sources'][source.relative_to(root).as_posix()]=digest(source)
   raw=obj.read_bytes();h=struct.unpack_from('<HHIIIIIHHHHHH',raw,16)
   _,funcs,_=sections(raw,h)
   for name in funcs:owners.setdefault(name,[]).append(source.name)
  (out/'ownership.json').write_text(json.dumps(owners,indent=2))
  linkscript=root/'device/s8/crt/link32.ld';receipt['sources'][linkscript.relative_to(root).as_posix()]=digest(linkscript)
  elf=out/'kernel.elf';execute([clang]+cxxflags+['-nostartfiles','-nostdlib']+objects+['-Wl,-Bstatic,--gc-sections,--emit-relocs,-T,'+str(linkscript)+',--defsym=STARTUP_ADDR=0x80000000,-Map,'+str(out/'kernel.map'),archive,'-L'+str(tools/'libc32/lib'),'-lc',tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a','-o',elf],'link.log')
  raw,h,loads,symbols=elf_info(elf);length=(max(symbols['_end']['value'],max(p['end'] for p in loads))-0x80000000+63)&~63;flat=bytearray(length)
  for p in loads:
   offset=p['start']-0x80000000;flat[offset:offset+p['file_bytes']]=raw[p['offset']:p['offset']+p['file_bytes']]
  (out/'code.bin').write_bytes(flat)
  for name,args in [('kernel.disasm',['llvm-objdump','-d']),('kernel.headers',['llvm-readelf','-h','-l','-S','-s','-r'])]:
   output=subprocess.check_output([str(tools/'llvm-vortex/bin'/args[0])]+args[1:]+[str(elf)]);(out/name).write_bytes(output)
  receipt['audit']=audit(elf,out/'code.bin',out/'kernel.map',out/'kernel.disasm',out/'layout.json')
  if source_digest(root)!=receipt['source_sha256']:raise ValueError('source changed during cross build')
  receipt.update(integrity='PASS',exit=0)
  for path in (clang,gcc,ar,tools/'riscv32-gnu-toolchain/bin/riscv32-unknown-elf-ld',Path(env['vortex_archive']),tools/'libc32/lib/libc.a',tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a'):
   receipt['dependencies'][str(path)]=digest(path)
 except Exception as exc:receipt.update(error=str(exc),exit=1)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 if receipt['exit']:raise ValueError(receipt['error'])
 return receipt
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('.'));p.add_argument('--env',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();r=build(a.root,a.env,a.out);print(json.dumps({k:v for k,v in r.items() if k not in ('commands','sources','dependencies','audit')}))
