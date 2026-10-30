"""Actual RV32 candidate compiler and static audit. Never executes the GPU."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT));sys.path.insert(0,str(ROOT/'scripts/hunet'))
from scripts.hunet_opt.catalog import validate_catalog
from audit_program import package_program,elf_info,instructions
def audit_instruction_words(words):
 r=dict(wmma=sum(w&0xfff0707f==0x0400000b for w in words),barrier=sum(w&0xfe007fff==0x0000400b for w in words),fence=words.count(0x0ff0000f))
 if not r['wmma'] or not r['barrier'] or r['fence']<2:raise ValueError('FROZEN_W4_ACTUAL_INSTRUCTION_CONTRACT')
 return r
def static_frame(words):
 values={0:0};total=0
 for w in words:
  op=w&127;rd=w>>7&31;f=w>>12&7;a=w>>15&31;rs2=w>>20&31;imm=w>>20;imm=imm-4096 if imm&2048 else imm
  if op==0x37:values[rd]=w&0xfffff000
  elif op==0x13 and f==0:
   if rd==2 and a==2 and imm<0:total-=imm
   if a in values:values[rd]=(values[a]+imm)&0xffffffff
   else:values.pop(rd,None)
  elif op==0x33 and f==0 and rd==2 and a==2:
   if rs2 not in values:raise ValueError('DYNAMIC_EXTERNAL_STACK')
   value=values[rs2];value=value-2**32 if value>=2**31 else value
   if w>>25==0x20 and value>0:total+=value
   elif w>>25==0 and value<0:total-=value
  elif op in (3,0x13,0x17,0x33,0x6f,0x67,0x73):values.pop(rd,None)
 return total
def external_stack_envelope(b,loads,symbols,frames):
 compiled={f['function'].split(':')[-1] for f in frames};external={};words=[]
 for name,s in symbols.items():
  if not s['size'] or not any(p['flags']&1 and p['start']<=s['value']<p['end'] for p in loads):continue
  raw=instructions(b,loads,symbols,name,s['size']//4);words.extend(raw)
  if name not in compiled:external[name]=static_frame(raw)
 # Sum every compiled frame, including mutually exclusive/unlinked functions,
 # then every fixed library frame; this conservatively contains a nonrecursive
 # source-bound numerical/callback chain. CRT stack pivot is audited separately.
 bound=sum({f['function']:f['bytes'] for f in frames}.values())+sum(external.values())
 if bound>=8192:raise ValueError('STACK_ENVELOPE_EXCEEDS_STRIDE')
 return dict(admission='PASS',bytes=bound,external_frames=external,scope='CONSERVATIVE_NONRECURSIVE_SOURCE_BOUND_FRAME_SUM_NOT_DYNAMIC_HIGH_WATER',condition='frozen numerical/callback paths; fixed diagnostic formats; CRT stack pivot audited separately'),audit_instruction_words(words)
DEFINES=['-D'+v for v in ('NUM_CLUSTERS=1','NUM_CORES=4','SOCKET_SIZE=4','NUM_WARPS=4','NUM_THREADS=4','NUM_BARRIERS=2','GBAR_ENABLE','ICACHE_SIZE=16384','DCACHE_SIZE=16384','DCACHE_WRITEBACK=0','PLATFORM_MEMORY_NUM_BANKS=1','PLATFORM_MEMORY_DATA_SIZE=64','MEM_BLOCK_SIZE=64','MEM_ADDR_WIDTH=32','LMEM_BASE_ADDR=1879048192','FPU_DSP','EXT_TCU_ENABLE','TCU_DSP','EXT_TEX_ENABLE','HUNET_SCALAR_CHUNK=32','IO_COUT_ADDR=0x10002040','IO_MPM_ADDR=0x10002080','XLEN_32')]
SDK_SOURCES=('vx_start.S','vx_print.S','vx_print.c','vx_perf.c','vx_spawn.c')
def check_defines(defines):
 if any(v not in defines for v in DEFINES):raise ValueError('FROZEN_HW4_RV32_DEFINES')
 return 'PASS'
def stale_sdk_members(members):
 return [name for name in members if name.split('.')[0] in ('vx_start','vx_print','vx_perf','vx_spawn')]
def required_device_symbols(policy):
 return ['opt_norm_body']+(['opt_grouped_gemm_body'] if policy['locate'] or policy['pack'] or policy['tile_n']!=1 or policy['internal_timing'] or policy['views'] else [])
def clang_resource_dir(tools):
 paths=[p for p in (pathlib.Path(tools)/'llvm-vortex/lib/clang').glob('*') if (p/'include/stddef.h').is_file()]
 if len(paths)!=1:raise ValueError('EXACT_CLANG_RESOURCE_DIRECTORY_REQUIRED')
 return paths[0]
def stack_audit(frames,edges,roots):
 memo={};active=set()
 def visit(name):
  if name in memo:return memo[name]
  if name in active:raise ValueError('RECURSIVE_STACK:'+name)
  if name not in frames:raise ValueError('UNKNOWN_STACK_FRAME:'+name)
  active.add(name);n=frames[name]+max([visit(v) for v in edges.get(name,[])]+[0]);active.remove(name);memo[name]=n;return n
 maximum=max(visit(name) for name in roots)
 if maximum>=8192:raise ValueError('STACK_CHAIN_EXCEEDS_STRIDE')
 return dict(max_chain_bytes=maximum,stride=8192,scope='STATIC_DIRECT_CALL_CHAIN_NOT_DYNAMIC_EXECUTION')
def run(command,log):
 with pathlib.Path(log).open('xb') as f:p=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
 pathlib.Path(str(log)+'.exit').write_text(str(p.returncode)+'\n')
 if p.returncode:raise ValueError('COMMAND_EXIT:'+str(p.returncode)+':'+str(log))
def build(catalog,out,env,identity=None):
 catalog=pathlib.Path(catalog).resolve();c=json.loads(catalog.read_bytes());validate_catalog(c);out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 r=dict(scope='RV32_COMPILE_AND_STATIC_AUDIT_NOT_BOARD',candidate=c['candidate'],source_identity=identity,exit=None,board='NOT_RUN',gpu_cycles=None)
 try:
  from scripts.hunet_opt.build_identity import verify_snapshot
  r.update(verify_snapshot(ROOT,identity or env['source_identity']))
  r['catalog_sha256']=hashlib.sha256(catalog.read_bytes()).hexdigest()
  r['tool_archive_sha256']=env['tool_archive_sha256']
  if c['layout']['execution_policy']['frozen_wire']:
   origin=pathlib.Path(env['frozen_rv32']);program=json.loads((origin/'program.json').read_bytes());wire=(origin/'code.bin').read_bytes()
   if hashlib.sha256(wire).hexdigest()!=env['frozen_wire_sha256'] or program['wire_sha256']!=env['frozen_wire_sha256']:raise ValueError('FROZEN_WIRE_IDENTITY')
   for name in ('kernel.elf','code.bin','kernel.vxbin','program.json'):
    if (origin/name).is_file():shutil.copyfile(origin/name,out/name)
   r.update(exit=0,mode='REUSED_BYTE_EXACT_PROGRAM9_WIRE_FOR_PROGRAM10',program=program,audit=dict(integrity='PASS',scope='FROZEN_WIRE_REUSE_NOT_NEW_GPU_COMPILE'))
  else:
   tools=pathlib.Path(env['rv32_tools']);sdk=ROOT/'vendor/hunet/sdk-linux-crt';gnu=tools/'riscv32-gnu-toolchain';gcc=str(gnu/'bin/riscv32-unknown-elf-gcc');ar=str(gnu/'bin/riscv32-unknown-elf-gcc-ar');clang=str(tools/'llvm-vortex/bin/clang++')
   (out/'sdk').mkdir();shutil.copyfile(env['vortex_archive'],out/'sdk/libvortex.a')
   check_defines(DEFINES);flags=list(DEFINES);policy=c['layout']['execution_policy']
   flags+=['-DHUNET_OPT_'+k+'='+str(int(v)) for k,v in [('GN',policy['parallel_gn']),('LOCATE',policy['locate']),('PACK',policy['pack']),('TILE_N',policy['tile_n']),('INTERNAL_TIMING',policy['internal_timing']),('VIEWS',policy['views']),('SCALAR_CHUNK',policy['scalar_chunk'])]]
   if policy['views']:
    flags+=['-DHUNET_OPT_VIEWS_HEADER="../generated/hunet_opt/'+c['candidate']+'/views.h"','-DHUNET_OPT_GRAPH_HEADER="../generated/hunet_opt/'+c['candidate']+'/device_graph.h"']
   common=['--sysroot='+str(gnu/'riscv32-unknown-elf'),'--gcc-toolchain='+str(gnu),'-Xclang','-target-feature','-Xclang','+vortex','-Xclang','-target-feature','-Xclang','+zicond','-mllvm','-disable-loop-idiom-all','-march=rv32imaf','-mabi=ilp32f','-std=c++17','-O2','-DNDEBUG','-ffp-contract=off','-fstack-usage','-mcmodel=medany','-fno-rtti','-fno-exceptions','-nostartfiles','-nostdlib','-fdata-sections','-ffunction-sections']+flags+['-I'+str(p) for p in (ROOT/'include',ROOT/'vendor/hunet/v5/include',sdk/'kernel/include',sdk/'sim/common',sdk/'hw')]
   resource=clang_resource_dir(tools);common+=['-resource-dir='+str(resource)]
   r.update(defines=flags,compile_flags=common,compiler_sha256=hashlib.sha256(pathlib.Path(clang).read_bytes()).hexdigest(),clang_resource_dir=str(resource),tool_archive_sha256=env['tool_archive_sha256'])
   # Replace the exact scheduler too; no inherited archive copy of vx_spawn.
   for name in SDK_SOURCES:
    cmd=[gcc,'-march=rv32imaf','-mabi=ilp32f','-O3','-mcmodel=medany','-fno-exceptions','-fdata-sections','-ffunction-sections','-fstack-usage']+flags+['-I'+str(sdk/'kernel/include'),'-I'+str(sdk/'hw'),'-c',str(sdk/'kernel/src'/name),'-o',str(out/'sdk'/(name+'.o'))]
    run(cmd,out/('compile-'+name+'.log'))
   members=subprocess.check_output([ar,'t',str(out/'sdk/libvortex.a')],universal_newlines=True).splitlines();stale=stale_sdk_members(members)
   if stale:run([ar,'d',str(out/'sdk/libvortex.a')]+stale,out/'archive-remove-stale.log')
   run([ar,'rcs',str(out/'sdk/libvortex.a')]+[str(out/'sdk'/(n+'.o')) for n in SDK_SOURCES],out/'archive.log')
   sources=[('v5',ROOT/'vendor/hunet/v5/src/device/kernel.cpp',['-Dmain=hunet_v5_entry']),('fused',ROOT/'device/hunet_fused.cpp',['-Dmain=hunet_fused_entry']),('opt',ROOT/'device/hunet_opt.cpp',[]),('gelu',ROOT/'vendor/hunet/v5/src/device/gelu_math.c',[])]
   for name,path,extra in sources:run([clang]+common+extra+['-MMD','-MF',str(out/(name+'.d')),'-c',str(path),'-o',str(out/(name+'.o'))],out/('compile-'+name+'.log'))
   run([clang]+common+[str(out/(n+'.o')) for n,_,_ in sources]+['-Wl,-Bstatic,--gc-sections,-T,'+str(sdk/'kernel/scripts/link32.ld')+',--defsym=STARTUP_ADDR=0x80000000,-Map,'+str(out/'kernel.map'),str(out/'sdk/libvortex.a'),'-L'+str(tools/'libc32/lib'),'-lm','-lc',str(tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a'),'-o',str(out/'kernel.elf')],out/'link.log')
   r['exit']=0;program=package_program(out/'kernel.elf',out,10);r['program']=program
   disasm=subprocess.check_output([str(tools/'llvm-vortex/bin/llvm-objdump'),'-d',str(out/'kernel.elf')],universal_newlines=True);(out/'kernel.disasm').write_text(disasm)
   headers=subprocess.check_output([str(tools/'llvm-vortex/bin/llvm-readelf'),'-h','-l',str(out/'kernel.elf')],universal_newlines=True);(out/'kernel.headers').write_text(headers)
   binary,_,loads,symbols=elf_info(out/'kernel.elf')
   for required in required_device_symbols(policy):
    if not any(required in n for n in symbols):raise ValueError('ACTUAL_OPT_GPU_ENTRY_MISSING:'+required)
   frames=[]
   for p in out.rglob('*.su'):
    for line in p.read_text().splitlines():
     name,size,kind=line.rsplit('\t',2);frames.append(dict(function=name,bytes=int(size),kind=kind))
   if not frames or any(f['kind']!='static' or f['bytes']>=8192 for f in frames):raise ValueError('COMPILED_STACK_FRAME_BOUND')
   r['stack_frames']=frames;r['max_frame_bytes']=max(f['bytes'] for f in frames)
   r['stack_envelope'],r['actual_instructions']=external_stack_envelope(binary,loads,symbols,frames)
   # Source callback chain plus caller-side scheduling frames overlap on the same stack.
   # Conservative sum of all compiled device/scheduler frames also bounds every direct chain,
   # but can refuse a valid candidate. Keep that refusal rather than silently truncating it.
   unique={f['function']:f['bytes'] for f in frames};bound=sum(unique.values());r['stack_conservative_sum_bytes']=bound
   r['stack_static_admission']='PASS' if bound<8192 else 'REQUIRES_CALLGRAPH_AUDIT'
   r['stack_instruction_references']=len(re.findall(r'\b(?:lw|sw|flw|fsw)\b[^\n]*\(sp\)',disasm));r['allocator_spills']='NOT_INFERRED_FROM_ALL_STACK_REFERENCES'
   r['dependencies']={str(path):hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest() for path in (pathlib.Path(clang),pathlib.Path(gcc),pathlib.Path(env['vortex_archive']),out/'sdk/libvortex.a',tools/'libc32/lib/libm.a',tools/'libc32/lib/libc.a',sdk/'kernel/src/vx_spawn.c')}
   r['audit']=dict(integrity='PASS' if bound<8192 else 'PENDING_STACK_CHAIN',scope='ACTUAL_RV32_STATIC_NOT_GPU_RUNTIME')
 except Exception as e:r.update(error=str(e),audit=dict(integrity='FAIL',reason=str(e)))
 (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
 if r.get('error'):raise ValueError(r['error'])
 return r
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--catalog',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--env',type=pathlib.Path,required=True);a=p.parse_args();print(json.dumps(build(a.catalog,a.out,json.loads(a.env.read_bytes()))))
