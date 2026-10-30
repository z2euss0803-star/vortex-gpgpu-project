"""Compile new controlled HUNet0x10010 with frozen WMMA/scalar math.
Static ISA/ownership/stack evidence only; no GPU, board or full graph claim.
"""
import argparse,hashlib,json,pathlib,shutil,struct,subprocess,re
from scripts.hunet_opt.build_rv32 import DEFINES,clang_resource_dir
from scripts.hunet.audit_program import instructions
from scripts.s8.audit_rv32 import sections,call_graph,array_callbacks,_signed
from scripts.unified.audit_int16_layout import audit as audit_owned_layout,elf_info
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def build(root,env_path,out,dispatch_variant="span32-v2"):
 if dispatch_variant not in ("scalar-v1","span32-v2","span32-tile2-v3"):raise ValueError("explicit HUNet dispatch variant")
 root=pathlib.Path(root).resolve();env=json.loads(pathlib.Path(env_path).read_text());out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U12_ACTUAL_HUNET_RV32_STATIC_NOT_FULL_MIGRATION_NOT_EXECUTED',natural_exit=1,gpu_raw_cycles=None,commands=[],sources={},dependencies={})
 def run(argv,label):
  argv=[str(a) for a in argv];p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/(label+'.log');log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,log=log.name,sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
  if p.returncode:raise ValueError(label+' exit='+str(p.returncode)+' '+p.stdout.decode('utf8','replace')[-1600:])
 try:
  tools=pathlib.Path(env['rv32_tools']);gnu=tools/'riscv32-gnu-toolchain';clang=tools/'llvm-vortex/bin/clang++';gcc=gnu/'bin/riscv32-unknown-elf-gcc';ar=gnu/'bin/riscv32-unknown-elf-gcc-ar';sdk=root/'vendor/hunet/sdk-linux-crt'
  version=subprocess.check_output([str(clang),'--version']).decode();assert '18.1.7' in version;rec['compiler_version']=version
  defines=[d for d in DEFINES if not d.startswith(('-DIO_MPM_ADDR=','-DHUNET_SCALAR_CHUNK='))]+['-DIO_MPM_ADDR=0x10002000']
  if dispatch_variant=='span32-tile2-v3':defines+=['-DVXNN_HUNET_TILE_N=2']
  flags=['-march=rv32imaf','-mabi=ilp32f','-O2','-DNDEBUG','-mcmodel=medany','-fno-fast-math','-ffp-contract=off','-fstack-usage','-fdata-sections','-ffunction-sections']+defines+['-I'+str(p) for p in (root,root/'include',root/'vendor/hunet/v5/include',sdk/'kernel/include',sdk/'kernel/src',sdk/'sim/common',sdk/'hw')]
  cxx=['--sysroot='+str(gnu/'riscv32-unknown-elf'),'--gcc-toolchain='+str(gnu),'-resource-dir='+str(clang_resource_dir(tools)),'-Xclang','-target-feature','-Xclang','+vortex','-Xclang','-target-feature','-Xclang','+zicond','-mllvm','-disable-loop-idiom-all','-std=c++17','-fno-exceptions','-fno-rtti']+flags
  archive=out/'libvortex.a';shutil.copyfile(env['vortex_archive'],archive);members=subprocess.check_output([str(ar),'t',str(archive)]).decode().splitlines();old=[n for n in members if n.split('.')[0] in ('vx_start','vx_print','vx_perf','vx_spawn','vx_syscalls')]
  if old:run([ar,'d',archive]+old,'prune-sdk-copy')
  units=[(root/'device/mosaic_s8/crt/vx_start.S',gcc,flags),(sdk/'kernel/src/vx_spawn.c',gcc,flags),(sdk/'kernel/src/vx_syscalls.c',gcc,flags),(root/'device/s8/crt/perf_stub.c',gcc,flags)]+[(root/n,clang,cxx) for n in ('device/vxnn/hunet_entry.cpp','device/vxnn/hunet_wire.cpp')]
  objects=[];owners={}
  for i,(source,compiler,options) in enumerate(units):
   obj=out/('%02d-'%i+source.name+'.o');run([compiler]+options+['-MMD','-MF',str(obj)+'.d','-c',source,'-o',obj],'compile-%02d'%i);objects.append(obj);rec['sources'][str(source.relative_to(root))]=sha(source)
   raw=obj.read_bytes();header=struct.unpack_from('<HHIIIIIHHHHHH',raw,16);_,funcs,_=sections(raw,header)
   for n in funcs:owners.setdefault(n,[]).append(source.name)
   for n in pathlib.Path(str(obj)+'.d').read_text().replace('\\\n',' ').split()[1:]:
    p=pathlib.Path(n)
    if p.is_file():rec['dependencies'][str(p)]=sha(p)
  # Link a derived read-only copy of the verified expf constant pool; do
  # not modify frozen libc or admit initialized mutable CODE.
  from scripts.unified.hunet_rodata import frozen,check_derived
  libc=tools/'libc32/lib/libc.a';exp_original=out/'expf-original.o';exp_original.write_bytes(subprocess.check_output([str(ar),'p',str(libc),'expf.o']));frozen(exp_original.read_bytes())
  exp_readonly=out/'expf-readonly.o';objcopy=gnu/'bin/riscv32-unknown-elf-objcopy'
  run([objcopy,'--rename-section','.sdata=.rodata.vxnn.expf.constants,alloc,load,readonly,data,contents',exp_original,exp_readonly],'place-expf-readonly')
  rec['expf_readonly']=check_derived(exp_original.read_bytes(),exp_readonly.read_bytes());objects.append(exp_readonly)
  script=root/'device/vxnn/link32.ld';elf=out/'kernel.elf';rec['sources'][str(script.relative_to(root))]=sha(script)
  run([clang]+cxx+['-nostartfiles','-nostdlib']+objects+['-Wl,-Bstatic,--gc-sections,--emit-relocs,-T,'+str(script)+',--defsym=STARTUP_ADDR=0x80000000,-Map,'+str(out/'kernel.map'),archive,'-L'+str(tools/'libc32/lib'),'-lc',tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a','-o',elf],'link')
  raw,eh,loads,symbols=elf_info(elf);assert eh[6]&7==2 and loads
  end=symbols['_end']['value'];stride=symbols['__tbss_size']['value'];assert not symbols['__tdata_size']['value'] and 0<stride<=4096 and stride%16==0 and end%16==0 and 0x10002400<=end and end+64*stride<=0x10002fc0
  # CODE remains byte-immutable across jobs/reloads. The frozen CRT writes all
  # BSS/TLS inside its owned4KiB PS object, not into/outside hashed CODE.
  code_bytes=65536;flat=bytearray(code_bytes)
  for p in loads:
   if p['start']>=0x80000000:
    assert p['end']<=0x8000ffc0;off=p['start']-0x80000000;flat[off:off+p['file_bytes']]=raw[p['offset']:p['offset']+p['file_bytes']]
   else:assert p['start']>=0x10002000 and p['end']<=0x10002fc0 and (not p['file_bytes'] or(p['start']==0x10002000 and p['offset']==0 and p['file_bytes']==eh[4]+eh[9]*32))
  flat[-64:]=bytes([0xa7])*64
  (out/'code.bin').write_bytes(flat)
  (out/'kernel.disasm').write_bytes(subprocess.check_output([str(tools/'llvm-vortex/bin/llvm-objdump'),'-d',str(elf)]))
  (out/'kernel.headers').write_bytes(subprocess.check_output([str(tools/'llvm-vortex/bin/llvm-readelf'),'-h','-l','-S','-s','-r',str(elf)]))
  named,funcs,undefined=sections(raw,eh);assert not undefined,undefined
  assert not any(n in symbols for n in ('malloc','calloc','realloc','free','_sbrk','vx_putchar'))
  frames={}
  for p in out.glob('*.su'):
   for line in p.read_text().splitlines():
    n,size,kind=line.rsplit('\t',2);assert kind=='static';frames[n.split(':')[-1]]=int(size)
  expected={'pack','gemm','scalar'}
  if dispatch_variant in ('span32-v2','span32-tile2-v3'):expected.update({'gn_apply','add','mul','copy','slice','concat','shift','gn_stats','gap'})
  callbacks=sorted(n for n in funcs if n.endswith('EPv') and 'hunet_controlled_' in n)
  names=[re.search(r'hunet_controlled_([a-z_]+)EPv$',n).group(1) for n in callbacks]
  assert len(names)==len(expected) and set(names)==expected,'EXACT_CONTROLLED_CALLBACK_SET_REQUIRED '+repr(names)
  rec['dispatch_variant']=dispatch_variant;rec['audited_callback_names']=names
  indirect={n:callbacks for n in funcs if 'vx_spawn.c' in owners.get(n.split('@')[0],[])}
  stack,wordmap=call_graph(raw,loads,funcs,owners,frames,array_callbacks(raw,named,funcs),indirect)
  words=[w for v in wordmap.values() for w in v]
  assert 0x00301073 in instructions(raw,loads,symbols,'init_regs',32),'all-hart RNE/FCSR initialization missing'
  assert any(w&0xfff0707f==0x0400000b for w in words),'native FP16 WMMA instruction missing'
  assert not any(w&127 in (0x43,0x47,0x4b,0x4f) for w in words),'frozen HUNet arithmetic must not introduce FP FMA'
  exit_words=wordmap.get('_Exit',[]);assert all(w in exit_words for w in (0x00050413,0xcc202373,0x0082a023,0x0000000b))
  regs={0:0};exit_address=None
  for w in exit_words:
   rd=w>>7&31;rs=w>>15&31;op=w&127;fn=w>>12&7
   if op==0x37:regs[rd]=w&0xfffff000
   elif op==0x13 and fn==0 and rs in regs:regs[rd]=(regs[rs]+_signed(w>>20,12))&0xffffffff
   elif op==0x23 and fn==2 and w>>20&31==8:exit_address=(regs.get(rs,0)+_signed((w>>25<<5)|(w>>7&31),12))&0xffffffff
  assert exit_address==0x10002008 and any(w&0x707f==0xf for w in exit_words)
  for n,d in rec['sources'].items():assert sha(root/n)==d,'source drift'
  for n,d in rec['dependencies'].items():assert sha(n)==d,'dependency drift'
  for p in (clang,gcc,ar,pathlib.Path(env['vortex_archive']),tools/'libc32/lib/libc.a',tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a'):rec['dependencies'][str(p)]=sha(p)
  (out/'ownership.json').write_text(json.dumps(owners,indent=2)+'\n');(out/'stack-audit.json').write_text(json.dumps(stack,indent=2)+'\n')
  rec.update(natural_exit=0,elf=dict(bytes=len(raw),sha256=sha(elf)),flat=dict(bytes=len(flat),sha256=sha(out/'code.bin')),stack=stack,runtime=dict(entry=0x80000000,args_gpu=0x80100040,args_bytes=128,crt_gpu=0x10002000,crt_bytes=4096,retire_offset=8,retire_stride=256,retire_value=0,retire_count=4,tls_base=end,tls_end=end+64*stride,tbss_stride=stride,runtime_bytes=65536,mutable_state_domain='PS_CRT',stack_gpu=0xfff00000,stack_bytes=0x100000,stack_stride=8192,rodata_bytes=0,fcsr_init=0,guards=[dict(gpu=0x80000000,offset=0xffc0,bytes=64,stride=64,count=1),dict(gpu=0x10002000,offset=0xfc0,bytes=64,stride=64,count=1),dict(gpu=0xfff00000,offset=0x70000,bytes=64,stride=8192,count=64)]),logical_program=0x10010,opcodes=[4,5,7,8,9,10,11,12,13,14,15],target_executed=False)
  rec['object_ownership']=audit_owned_layout(elf,out/'code.bin',rec)
 except Exception as exc:rec.update(natural_exit=1,error=repr(exc))
 (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 if rec['natural_exit']:raise ValueError(rec['error'])
 return rec
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',default='.');p.add_argument('--env',required=True);p.add_argument('--out',required=True);p.add_argument('--dispatch-variant',choices=('scalar-v1','span32-v2','span32-tile2-v3'),default='span32-v2');a=p.parse_args();r=build(a.root,a.env,a.out,a.dispatch_variant);print(json.dumps(dict(scope=r['scope'],natural_exit=r['natural_exit'],flat=r['flat'],runtime=r['runtime'],gpu_raw_cycles=None)))
