"""Compile new Mosaic native INT16 library plus frozen readonly norm tables.

One controlled library serves all eight Mosaic primitive opcodes.
Old program identities are not edited or reused. Actual catalog admission and
application bindings are later gates; this produces compile/static evidence.
"""
import argparse,hashlib,json,pathlib,shutil,struct,subprocess
from scripts.hunet_opt.build_rv32 import DEFINES,clang_resource_dir
from scripts.hunet.audit_program import instructions
from scripts.s8.audit_rv32 import sections,call_graph,array_callbacks,_signed
from scripts.unified.audit_mosaic_layout import audit as audit_owned_layout,elf_info
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def validate_variant(root,variant):
 root=pathlib.Path(root).resolve()
 if variant in ('legacy-v1','ro64-v2','fused-pointwise-v4'):
  raise ValueError('FROZEN_R6_SOURCE_REQUIRED: old entries do not bind the cooperative raw-page buffer in this tree; use the immutable R6 source/publication')
 if variant not in ('cooperative-v6','reuse-v7'):raise ValueError('EXPLICIT_MOSAIC_V6_OR_V7_VARIANT_REQUIRED')
 if variant=='cooperative-v6':
  family=json.loads((root/'data/nnrt/programs/mosaic-int16-v6/family.json').read_bytes())
  if family.get('variant')!='cooperative-v6' or any(sha(root/row['path'])!=row['sha256'] for row in family['sources']):
   raise ValueError('EXACT_PUBLISHED_V6_SOURCES_REQUIRED: shared GPU sources changed; build reuse-v7 or use frozen R8')
 return variant

def build(root,env_path,out,variant="reuse-v7"):
 validate_variant(root,variant)
 root=pathlib.Path(root).resolve();env=json.loads(pathlib.Path(env_path).read_text());out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U13_MOSAIC_ACTUAL_RV32_STATIC_NOT_EXECUTED',natural_exit=1,gpu_raw_cycles=None,commands=[],sources={},dependencies={},variant=variant,private_abi=4 if variant=='reuse-v7' else 3,schedule_version=2 if variant=='reuse-v7' else 1,weight_layout_version=2 if variant=='reuse-v7' else 1,work_layout_version=1,profile_wire_version=2)
 def run(argv,label):
  argv=[str(a) for a in argv];p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/(label+'.log');log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,log=log.name,sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
  if p.returncode:raise ValueError(label+' exit='+str(p.returncode)+' '+p.stdout.decode('utf8','replace')[-1600:])
 try:
  tools=pathlib.Path(env['rv32_tools']);gnu=tools/'riscv32-gnu-toolchain';clang=tools/'llvm-vortex/bin/clang++';gcc=gnu/'bin/riscv32-unknown-elf-gcc';ar=gnu/'bin/riscv32-unknown-elf-gcc-ar';sdk=root/'vendor/hunet/sdk-linux-crt'
  version=subprocess.check_output([str(clang),'--version']).decode();assert '18.1.7' in version;rec['compiler_version']=version
  defines=[d for d in DEFINES if not d.startswith(('-DIO_MPM_ADDR=','-DHUNET_SCALAR_CHUNK='))]+['-DIO_MPM_ADDR=0x10002000']
  flags=['-march=rv32imaf','-mabi=ilp32f','-O2','-mcmodel=medany','-msmall-data-limit=0','-fno-fast-math','-ffp-contract=off','-fstack-usage','-fdata-sections','-ffunction-sections']+defines+['-I'+str(p) for p in (root,root/'include',sdk/'kernel/include',sdk/'kernel/src',sdk/'sim/common',sdk/'hw')]
  cxx=['--sysroot='+str(gnu/'riscv32-unknown-elf'),'--gcc-toolchain='+str(gnu),'-resource-dir='+str(clang_resource_dir(tools)),'-Xclang','-target-feature','-Xclang','+vortex','-Xclang','-target-feature','-Xclang','+zicond','-mllvm','-disable-loop-idiom-all','-std=c++17','-fno-exceptions','-fno-rtti']+flags
  archive=out/'libvortex.a';shutil.copyfile(env['vortex_archive'],archive);members=subprocess.check_output([str(ar),'t',str(archive)]).decode().splitlines();old=[n for n in members if n.split('.')[0] in ('vx_start','vx_print','vx_perf','vx_spawn','vx_syscalls')]
  if old:run([ar,'d',archive]+old,'prune-sdk-copy')
  wire_source='device/vxnn/mosaic_wire_ro64.cpp' if variant in ('ro64-v2','fused-pointwise-v4','cooperative-v6','reuse-v7') else 'device/vxnn/mosaic_wire.cpp'
  entry_source='device/vxnn/mosaic_reuse_entry.cpp' if variant=='reuse-v7' else 'device/vxnn/mosaic_coop_entry.cpp'
  units=[(root/'device/mosaic_s8/crt/vx_start.S',gcc,flags),(sdk/'kernel/src/vx_spawn.c',gcc,flags),(sdk/'kernel/src/vx_syscalls.c',gcc,flags),(root/'device/s8/crt/perf_stub.c',gcc,flags)]+[(root/n,clang,cxx) for n in (entry_source,wire_source,'device/vxnn/mosaic_ops.cpp','device/mosaic_s8/conv.cpp','device/mosaic_s8/quant_pack.cpp','device/mosaic_s8/instance_norm_compat_v2.cpp','device/mosaic_s8/instance_norm_compat_v3.cpp','device/mosaic_s8/tile.cpp','device/mosaic_s8/unpack.cpp','device/mosaic_s8/isa_adapter.cpp')]
  # The shared entry always names this wrapper; ABI1 keeps its chosen old base validator.
  units.insert(5,(root/'device/vxnn/mosaic_wire_fused_v1.cpp',clang,cxx))
  if variant in ('cooperative-v6','reuse-v7'):
   units.extend((root/n,clang,cxx) for n in ('device/vxnn/mosaic_wire_coop_v3.cpp','device/vxnn/mosaic_point_lane.cpp','device/vxnn/mosaic_coop.cpp'))
  if variant=='reuse-v7':units.append((root/'device/vxnn/mosaic_wire_reuse_v4.cpp',clang,cxx))
  objects=[];owners={}
  for i,(source,compiler,options) in enumerate(units):
   obj=out/('%02d-'%i+source.name+'.o');options=list(options)
   if source.name in ('instance_norm_compat_v2.cpp','instance_norm_compat_v3.cpp'):options+=['-DMOSAIC_NORM_COMPAT_EXPLICIT_ISA_FMA','-include',str(root/'device/mosaic_s8/norm_explicit_fma_prefix.h')]
   run([compiler]+options+['-MMD','-MF',str(obj)+'.d','-c',source,'-o',obj],'compile-%02d'%i);objects.append(obj);rec['sources'][str(source.relative_to(root))]=sha(source)
   raw=obj.read_bytes();header=struct.unpack_from('<HHIIIIIHHHHHH',raw,16);_,funcs,_=sections(raw,header)
   for n in funcs:owners.setdefault(n,[]).append(source.name)
   for n in pathlib.Path(str(obj)+'.d').read_text().replace('\\\n',' ').split()[1:]:
    p=pathlib.Path(n)
    if p.is_file():rec['dependencies'][str(p)]=sha(p)
  norm=json.loads((root/'config/mosaic/norm-compat-v3.json').read_text());table_paths=[]
  table_inputs=env.get('mosaic_norm_tables',{})
  if not isinstance(table_inputs,dict) or set(table_inputs)-{'rcp.f32','rsqrt.bin'}:raise ValueError('explicit frozen norm table input names')
  for key in ('rcp.f32','rsqrt.bin'):
   row=norm['tables'][key];path=pathlib.Path(table_inputs.get(key,row['remote_path']));assert path.is_absolute() and path.stat().st_size==row['bytes'] and sha(path)==row['sha256'];table_paths.append(path);rec['dependencies'][str(path)]=row['sha256']
  table=out/'mosaic-tables.S';text='.section .mosaic_norm_lookup,"a",@progbits\n.balign 64\n'
  for symbol,path in zip(('mosaic_norm_rcp_count_v2','mosaic_norm_rsqrt_delta_v2'),table_paths):text+='.global '+symbol+'\n.type '+symbol+',@object\n'+symbol+':\n.incbin "'+str(path)+'"\n.size '+symbol+', .-'+symbol+'\n'
  table.write_text(text);obj=out/'mosaic-tables.o';run([gcc]+flags+['-c',table,'-o',obj],'compile-readonly-tables');objects.append(obj)
  script=root/'device/vxnn/mosaic_link32.ld';elf=out/'kernel.elf';rec['sources'][str(script.relative_to(root))]=sha(script)
  run([clang]+cxx+['-nostartfiles','-nostdlib']+objects+['-Wl,-Bstatic,--gc-sections,--emit-relocs,-T,'+str(script)+',--defsym=STARTUP_ADDR=0x80000000,-Map,'+str(out/'kernel.map'),archive,gnu/'riscv32-unknown-elf/lib/libm.a','-L'+str(tools/'libc32/lib'),'-lc',tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a','-o',elf],'link')
  raw,eh,loads,symbols=elf_info(elf);assert eh[6]&7==2 and loads
  end=symbols['_end']['value'];stride=symbols['__tbss_size']['value'];assert not symbols['__tdata_size']['value'] and 0<stride<=4096 and stride%16==0 and end%16==0 and 0x10002400<=end and end+64*stride<=0x10002fc0
  # CODE remains byte-immutable across jobs/reloads. The frozen CRT writes all
  # BSS/TLS inside its owned4KiB PS object, not into/outside hashed CODE.
  code_bytes=65536;flat=bytearray(code_bytes);robytes=None
  for p in loads:
   if 0x80000000<=p['start']<0x80100000:
    assert p['end']<=0x8000ffc0;off=p['start']-0x80000000;flat[off:off+p['file_bytes']]=raw[p['offset']:p['offset']+p['file_bytes']]
   elif p['start']==0x80110000:
    assert p['end']==0x80110000+6307840 and p['file_bytes']==p['memory_bytes']==6307840 and not(p['flags']&3);robytes=raw[p['offset']:p['offset']+p['file_bytes']]
   else:assert p['start']>=0x10002000 and p['end']<=0x10002fc0 and (not p['file_bytes'] or(p['start']==0x10002000 and p['offset']==0 and p['file_bytes']==eh[4]+eh[9]*32))
  assert robytes==b''.join(p.read_bytes() for p in table_paths);(out/'rodata.bin').write_bytes(robytes)
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
  native=[n for n in funcs if any(t in n for t in ('native_begin','native_step','native_read','native_release','native_fence','native_now','native_broadcast','native_agree','native_synchronize'))];callbacks=[n for n in funcs if n.endswith('EPv') and 'callback' in n and pathlib.Path(entry_source).name in owners.get(n.split('@')[0],[])];assert native and len(callbacks)==1,'MOSAIC_NATIVE_ENTRY_PROTOCOL_OR_CALLBACK_MISSING'
  indirect={n:native for n in funcs if any(source in ('tile.cpp','conv.cpp','mosaic_coop.cpp','mosaic_coop_entry.cpp','mosaic_reuse_entry.cpp') for source in owners.get(n.split('@')[0],[]))}
  indirect.update({n:callbacks for n in funcs if 'vx_spawn.c' in owners.get(n.split('@')[0],[])})
  stack,wordmap=call_graph(raw,loads,funcs,owners,frames,array_callbacks(raw,named,funcs),indirect)
  words=[w for v in wordmap.values() for w in v]
  assert 0x00301073 in instructions(raw,loads,symbols,'init_regs',32),'all-hart RNE/FCSR initialization missing'
  assert all(0x0400040b+(fmt<<15) in words for fmt in (13,14,15)),'exact native signed INT16 PE begin/step/read missing'
  fma_functions=[]
  for fn,body in wordmap.items():
   if any(w&127 in (0x43,0x47,0x4b,0x4f) for w in body):
    found=[w for w in body if w&127 in (0x43,0x47,0x4b,0x4f)];assert all((w>>12&7)==0 for w in found),'FMA_RNE_REQUIRED '+fn;origin=owners.get(fn.split('@')[0],[]);assert origin and all(n in ('instance_norm_compat_v2.cpp','instance_norm_compat_v3.cpp') for n in origin),'FMA_OUTSIDE_INSTANCE_NORM '+fn;fma_functions.append(fn)
  assert fma_functions,'explicit norm FMA missing';rec['instance_norm_fma_functions']=fma_functions
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
  rec.update(natural_exit=0,elf=dict(bytes=len(raw),sha256=sha(elf)),flat=dict(bytes=len(flat),sha256=sha(out/'code.bin')),stack=stack,runtime=dict(entry=0x80000000,args_gpu=0x80100040,args_bytes=128,crt_gpu=0x10002000,crt_bytes=4096,retire_offset=8,retire_stride=256,retire_value=0,retire_count=4,tls_base=end,tls_end=end+64*stride,tbss_stride=stride,runtime_bytes=65536,mutable_state_domain='PS_CRT',stack_gpu=0xfff00000,stack_bytes=0x100000,stack_stride=8192,rodata_gpu=0x80110000,rodata_bytes=6307840,fcsr_init=0,guards=[dict(gpu=0x80000000,offset=0xffc0,bytes=64,stride=64,count=1),dict(gpu=0x10002000,offset=0xfc0,bytes=64,stride=64,count=1),dict(gpu=0xfff00000,offset=0x70000,bytes=64,stride=8192,count=64)]),logical_program=0x10020,opcodes=list(range(1,9)) if variant in ("fused-pointwise-v4","cooperative-v6","reuse-v7") else list(range(1,7)),readonly=dict(bytes=len(robytes),sha256=sha(out/'rodata.bin')),target_executed=False)
  rec['object_ownership']=audit_owned_layout(elf,out/'code.bin',out/'rodata.bin',rec)
 except Exception as exc:rec.update(natural_exit=1,error=repr(exc))
 (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 if rec['natural_exit']:raise ValueError(rec['error'])
 return rec
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',default='.');p.add_argument('--env',required=True);p.add_argument('--out',required=True);p.add_argument('--variant',choices=('legacy-v1','ro64-v2','fused-pointwise-v4','cooperative-v6','reuse-v7'),default='reuse-v7');a=p.parse_args();r=build(a.root,a.env,a.out,a.variant);print(json.dumps(dict(scope=r['scope'],natural_exit=r['natural_exit'],flat=r['flat'],runtime=r['runtime'],gpu_raw_cycles=None)))
