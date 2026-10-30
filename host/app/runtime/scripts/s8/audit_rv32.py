"""Extract actual ELF32 bytes, fixed ISA and bounded static call graph. No execution."""
from pathlib import Path
import hashlib,json,struct
from scripts.hunet.audit_program import elf_info,instructions
from scripts.hunet_opt.build_rv32 import static_frame
from scripts.s8.crt import audit_facts

def stack_bound(frames,edges,roots):
 memo={};active=set()
 def visit(n):
  if n in memo:return memo[n]
  if n in active:raise ValueError('recursive stack: '+n)
  if n not in frames:raise ValueError('unknown stack: '+n)
  active.add(n);v=frames[n]+max([visit(c) for c in edges.get(n,[])]+[0]);active.remove(n);memo[n]=v;return v
 size=max(visit(r) for r in roots)
 if size>=8128:raise ValueError('stack reaches guarded stride')
 return size

def audit_words(words):
 formats=[f for f in (13,14,15) if any(w&0xffffffff==0x0400040b+(f<<15) for w in words)]
 if formats!=[13,14,15] or 0x0400800b not in words or not any(w&0xfe00707f==0x0000002b for w in words):raise ValueError('missing actual S8/FP16-to-FP32 WMMA/TEX machine instructions')
 if 0x0000000b not in words or not any(w&0x707f==0x000f for w in words):raise ValueError('missing real fence/tmc0')
 return dict(s8_formats=formats,wmma=True,tex=True)

def sections(raw,h):
 rows=[struct.unpack_from('<10I',raw,h[5]+i*h[10]) for i in range(h[11])]
 names=rows[h[12]];text=raw[names[4]:names[4]+names[5]]
 named={text[r[0]:].split(b'\0',1)[0].decode():r for r in rows}
 funcs={};undefined=[]
 for r in rows:
  if r[1]!=2:continue
  strings=rows[r[6]];st=raw[strings[4]:strings[4]+strings[5]]
  for o in range(r[4],r[4]+r[5],r[9]):
   n,v,size,info,other,index=struct.unpack_from('<IIIBBH',raw,o);name=st[n:].split(b'\0',1)[0].decode()
   if name and index==0:undefined.append(name)
   if name and index and info&15==2:
    key=name if name not in funcs or funcs[name]['value']==v else name+'@'+hex(v)
    funcs[key]=dict(value=v,size=size)
 return named,funcs,undefined

def _signed(x,b):return x-(1<<b) if x&(1<<(b-1)) else x

def array_callbacks(raw,named,funcs):
 result=dict(init=[],fini=[]);addresses={s['value']:n for n,s in funcs.items()}
 for name in ('.preinit_array','.init_array','.fini_array'):
  if name not in named:continue
  row=named[name];data=raw[row[4]:row[4]+row[5]]
  if len(data)!=row[5] or len(data)%4:raise ValueError('partial initializer table')
  for off in range(0,len(data),4):
   target=struct.unpack_from('<I',data,off)[0]
   if target not in addresses:raise ValueError('unknown initializer callback '+hex(target))
   result['fini' if name=='.fini_array' else 'init'].append(addresses[target])
 return result

def machine_frame(name,words,frames):
 if name=='init_regs':
  if not all(w in words for w in (0xffff0137,0xf14022f3,0x00d29313,0x40610133)):raise ValueError('unknown CRT initial stack pivot')
  return 0
 if name in frames:return frames[name]
 try:return static_frame(words)
 except ValueError as exc:raise ValueError('unbounded machine frame '+name+': '+str(exc))

def call_graph(raw,loads,funcs,owners,frames,arrays=None,typed_indirect=None):
 executable=[p for p in loads if p['flags']&1]
 funcs={n:s for n,s in funcs.items() if any(p['start']<=s['value']<p['start']+p['file_bytes'] for p in executable)}
 byaddr={}
 for n,s in sorted(funcs.items()):byaddr.setdefault(s['value'],n)
 edges={n:set() for n in byaddr.values()};indirect=[];words_by={}
 addresses=sorted(byaddr)
 natives=[n for n in edges if any(x in n for x in ('native_begin','native_step','native_read','native_fence','native_phase','native_contender','native_partial'))]
 cycles=[n for n in edges if 'phase.cpp' in owners.get(n.split('@')[0],[]) and (n.startswith('_ZL4high') or n.startswith('_ZL3low'))]
 callbacks=[n for n in edges if 's8_callback' in n]
 for a,n in byaddr.items():
  size=funcs[n]['size'] or (next((v for v in addresses if v>a),next(p['start']+p['file_bytes'] for p in executable if p['start']<=a<p['start']+p['file_bytes']))-a)
  entry=dict(funcs[n],size=size);words=instructions(raw,loads,{n:entry},n,size//4);words_by[n]=words
  if n not in frames:frames[n]=machine_frame(n.split('@')[0],words,frames)
  for i,w in enumerate(words):
   op=w&127;rd=w>>7&31;rs=w>>15&31;pc=a+i*4;target=None
   if op==0x6f:
    imm=((w>>31)<<20)|((w>>12&255)<<12)|((w>>20&1)<<11)|((w>>21&1023)<<1);target=(pc+_signed(imm,21))&0xffffffff
   elif op==0x67:
    imm=_signed(w>>20,12)
    if rd==0 and rs==1 and imm==0:continue
    if i and words[i-1]&127==0x17 and (words[i-1]>>7&31)==rs:target=(pc-4+_signed(words[i-1]&0xfffff000,32)+imm)&0xffffffff
    if target is None:
     owner=owners.get(n.split('@')[0],[])
     if typed_indirect is not None and n in typed_indirect:targets=typed_indirect[n]
     elif set(owner)&{'protocol.cpp','negative.cpp'}:targets=natives
     elif 'compat.cpp' in owner:targets=[x for x in edges if any(t in x for t in ('native_wmma','native_texture','native_fence'))]
     elif 'phase.cpp' in owner:targets=cycles
     elif 'vx_spawn.c' in owner:targets=callbacks
     elif n in ('__funcs_on_exit','libc_exit_fini','__libc_exit_fini','__libc_fini_array','__libc_init_array') and arrays is not None:
      targets=arrays['init' if n=='__libc_init_array' else 'fini']
      if not targets:
       indirect.append(dict(caller=n,pc=pc,source=owner,targets=[],reason='actual linked static initializer table is empty'));continue
     else:raise ValueError('unclosed indirect call '+n+'@'+hex(pc))
     if not targets:raise ValueError('missing typed callback targets '+n)
     edges[n].update(targets);indirect.append(dict(caller=n,pc=pc,source=owner,targets=targets));continue
   if target is not None:
    if a<=target<a+size:continue
    if target not in byaddr:raise ValueError('unknown call target '+n+' '+hex(target))
    edges[n].add(byaddr[target])
 roots=[n for n in edges if n in ('_start','init_regs_all','init_tls_all') or 's8_callback' in n]
 if not roots:raise ValueError('full CRT roots absent')
 bound=stack_bound(frames,edges,roots)
 return dict(bytes=bound,frames=frames,edges={n:sorted(v) for n,v in edges.items()},indirect=indirect,roots=roots,scope='CONSERVATIVE_TYPED_STATIC_CALL_GRAPH_NOT_RUNTIME_HIGH_WATER'),words_by

def audit(elf,flat,map_file,disasm,layout):
 elf,flat,map_file,disasm,layout=map(Path,(elf,flat,map_file,disasm,layout))
 data=elf.read_bytes()
 if len(data)<52:raise ValueError('partial ELF')
 raw,h,loads,symbols=elf_info(elf)
 if h[6]&7!=2 or not loads:raise ValueError('requires RV32IMAF single-float ABI, no compressed')
 named,funcs,undefined=sections(raw,h)
 if undefined:raise ValueError('unresolved ELF symbols '+str(undefined))
 if not map_file.is_file() or not disasm.is_file() or not map_file.stat().st_size or not disasm.stat().st_size:raise ValueError('missing actual map/disassembly')
 for n in ('malloc','calloc','realloc','free','_sbrk','vx_putchar'):
  if n in symbols:raise ValueError('forbidden heap/console symbol '+n)
 tdata=named.get('.tdata');tbss=named.get('.tbss')
 if not tbss or tbss[3]%16:raise ValueError('missing aligned TBSS')
 end=symbols['_end']['value'];stride=symbols['__tbss_size']['value']
 required=(max(end,max(x['end'] for x in loads))-0x80000000+63)&~63
 expected=bytearray(required)
 for p in loads:
  start=p['start']-0x80000000;expected[start:start+p['file_bytes']]=raw[p['offset']:p['offset']+p['file_bytes']]
 if flat.read_bytes()!=expected:raise ValueError('flat is not actual full PT_LOAD/BSS image')
 frames={};owners={}
 for p in elf.parent.rglob('*.su'):
  for line in p.read_text().splitlines():
   name,size,kind=line.rsplit('\t',2);name=name.split(':')[-1]
   if kind!='static':raise ValueError('dynamic/unknown stack frame '+name)
   frames[name]=int(size)
 ownership=elf.parent/'ownership.json'
 if not ownership.is_file() or not frames:raise ValueError('missing stack usage/compiled ownership')
 if any(n in funcs for n in ('atexit','__cxa_atexit','on_exit')):raise ValueError('dynamic exit registration not bounded')
 owners=json.loads(ownership.read_text());stack,wordmap=call_graph(raw,loads,funcs,owners,frames,array_callbacks(raw,named,funcs))
 allwords=[w for v in wordmap.values() for w in v];isa=audit_words(allwords)
 exit_words=wordmap.get('_Exit',[])
 # _Exit must preserve a0 in s0 before calling perf; private base+core*256,
 # store real s0 then fence then tmc0, from the frozen SDK CRT.
 if 0x00050413 not in exit_words or 0xcc202373 not in exit_words or 0x0082a023 not in exit_words or 0x0000000b not in exit_words:raise ValueError('real CRT return/core/store/tmc machine contract')
 regs={0:0};exit_address=None
 for w in exit_words:
  rd=w>>7&31;rs=w>>15&31;op=w&127;fn=w>>12&7
  if op==0x37:regs[rd]=w&0xfffff000
  elif op==0x13 and fn==0 and rs in regs:regs[rd]=(regs[rs]+_signed(w>>20,12))&0xffffffff
  elif op==0x23 and fn==2 and w>>20&31==8:exit_address=(regs.get(rs,0)+_signed((w>>25<<5)|(w>>7&31),12))&0xffffffff
 if exit_address!=0x10002008:raise ValueError('wrong CRT exit base')
 perf=wordmap.get('vx_perf_dump',[])
 if not perf or any((w&127)==0x73 for w in perf) or not any(w&0x707f==0xf for w in perf):raise ValueError('old performance dump present')
 facts=dict(entry=h[3],tdata_bytes=tdata[5] if tdata else 0,tbss_bytes=tbss[5],tbss_stride=stride,tbss_align=16,tls_base=end,static_stack_bytes=stack['bytes'],load_ranges=[dict(start=p['start'],mem_bytes=p['memory_bytes'],file_bytes=p['file_bytes']) for p in loads],exit_address=exit_address,stack_base=0xffff0000,stack_log2=13,cores=4,warps=4,lanes=4,has_exit_store=True,has_exit_fence=any(w&0x707f==0xf for w in exit_words),recursive=False,dynamic_stack=False,unresolved=undefined,runtime_symbols=list(symbols))
 admitted=audit_facts(facts);layout.write_text(json.dumps(admitted['layout'],sort_keys=True,indent=2)+'\n')
 return dict(scope='ACTUAL_RV32_STATIC_AUDIT_NOT_GPU_EXECUTION',integrity='PASS',facts=facts,stack=stack,instructions=isa,elf_sha256=hashlib.sha256(raw).hexdigest(),flat_sha256=hashlib.sha256(flat.read_bytes()).hexdigest(),flat_bytes=len(expected),gpu_raw_cycles=None)
