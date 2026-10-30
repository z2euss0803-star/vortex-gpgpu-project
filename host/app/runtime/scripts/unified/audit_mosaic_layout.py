"""Bind real RV32 memory to unchanged v2 owned CODE/CRT/ARGS/STACK objects.

The generic loader hashes the whole CODE object, and only declares CRT/stack
mutable. An ELF/link pass alone does not prove this ownership layout.
"""
import argparse,hashlib,json,pathlib,struct
from scripts.s8.audit_rv32 import sections
CODE=0x80000000;CODE_BYTES=65536;CRT=0x10002000;CRT_BYTES=4096;RO=0x80110000;RO_BYTES=6307840
def elf_info(path):
 # Separate bounded parser: the unchanged old NN parser admits only PL loads.
 # This binding additionally owns zero-initialized BSS/TLS inside PS CRT.
 b=pathlib.Path(path).read_bytes()
 if len(b)<52 or b[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('MOSAIC_ELF_FORMAT')
 h=struct.unpack_from('<HHIIIIIHHHHHH',b,16)
 if h[0]!=2 or h[1]!=243 or h[3]!=CODE or h[6]&7!=2 or h[7]!=52 or h[8]!=32 or h[10]!=40 or not 0<h[9]<=32 or not 0<h[11]<=4096 or h[12]>=h[11] or h[4]+h[9]*32>len(b) or h[5]+h[11]*40>len(b):raise ValueError('MOSAIC_ELF_TABLES')
 loads=[];header_end=h[4]+h[9]*32
 for i in range(h[9]):
  t,o,v,p,f,m,flags,align=struct.unpack_from('<8I',b,h[4]+i*32)
  if t not in (1,7,0x6474e551,0x70000003):raise ValueError('unsupported dynamic/other segment')
  if t!=1:continue
  if f>m or o+f>len(b) or v+m>0x100000000 or p!=v:raise ValueError('MOSAIC_ELF_LOAD_SIZE_OR_LMA')
  code=CODE<=v and v+m<=CODE+CODE_BYTES-64
  # GNU ld maps ELF+PHDR bytes in the first, lowest PS LOAD. They are metadata,
  # not allocated state sections; the existing loader zeros CRT instead.
  private=CRT<=v and v+m<=CRT+CRT_BYTES-64 and not(flags&1) and (f==0 or(v==CRT and o==0 and f==header_end))
  readonly=v==RO and f==m==RO_BYTES and not(flags&3)
  if not(code or private or readonly):raise ValueError('MOSAIC_ELF_OWNED_RANGE '+hex(v))
  loads.append(dict(offset=o,start=v,end=v+m,file_bytes=f,memory_bytes=m,flags=flags))
 if not loads:raise ValueError('MOSAIC_ELF_NO_LOAD')
 rows=[struct.unpack_from('<10I',b,h[5]+i*40) for i in range(h[11])];symbols={}
 for s in rows:
  if s[1]!=8 and s[4]+s[5]>len(b):raise ValueError('MOSAIC_ELF_SECTION_SIZE')
  if s[1]!=2:continue
  if s[9]!=16 or s[5]%16 or s[6]>=len(rows):raise ValueError('MOSAIC_ELF_SYMBOL_TABLE')
  strings=rows[s[6]];st=b[strings[4]:strings[4]+strings[5]]
  for j in range(s[4],s[4]+s[5],16):
   n,v,z,info,other,index=struct.unpack_from('<IIIBBH',b,j)
   if n>=len(st):raise ValueError('MOSAIC_ELF_SYMBOL_NAME')
   name=st[n:].split(b'\0',1)[0].decode('utf8','strict')
   if name:symbols[name]=dict(value=v,size=z)
 return b,h,loads,symbols
def audit(elf,wire,rodata,metadata):
 raw,h,loads,symbols=elf_info(pathlib.Path(elf));named,_,undefined=sections(raw,h)
 assert not undefined and h[6]&7==2
 state_first=CRT+0x400;state_limit=CRT+CRT_BYTES-64
 for name,row in named.items():
  if row[2]&2 and row[5] and CRT<=row[3]<CRT+CRT_BYTES:
   if row[1]!=8 or row[3]<state_first or row[3]+row[5]>state_limit:raise ValueError('private state section is not owned zero initialization: '+name)
 for name in ('.bss','.sbss'):
  if name in named:
   row=named[name]
   if row[5] and not(state_first<=row[3] and row[3]+row[5]<=state_limit):raise ValueError('unowned mutable '+name+' outside declared private CRT at '+hex(row[3]))
 for name in ('.data','.data1','.sdata','.data.rel.ro'):
  if name in named and named[name][5]:raise ValueError('initialized mutable state needs an explicit initialization contract: '+name)
 end=symbols['_end']['value'];stride=symbols['__tbss_size']['value']
 if not(state_first<=end and end%16==0 and 0<stride<=4096 and stride%16==0 and end+64*stride<=state_limit):raise ValueError('64-hart TLS is not owned by private CRT')
 if symbols['__tdata_size']['value'] or symbols['__tbss_offset']['value']:raise ValueError('nonzero TLS template/offset unsupported')
 linked_end=symbols['__vxnn_code_end']['value']
 if not CODE<linked_end<=CODE+CODE_BYTES-64:raise ValueError('code reaches immutable guard trailer')
 expected=bytearray(CODE_BYTES)
 for p in loads:
  if CODE<=p['start'] and p['end']<=CODE+CODE_BYTES-64:
   off=p['start']-CODE;expected[off:off+p['file_bytes']]=raw[p['offset']:p['offset']+p['file_bytes']]
  elif CRT<=p['start'] and p['end']<=state_limit:
   if p['file_bytes'] and not(p['start']==CRT and p['offset']==0 and p['file_bytes']==h[4]+h[9]*32):raise ValueError('private CRT contains actual initialized data')
  elif p['start']==RO and p['end']==RO+RO_BYTES and p['file_bytes']==RO_BYTES and not(p['flags']&3):
   if pathlib.Path(rodata).read_bytes()!=raw[p['offset']:p['offset']+RO_BYTES]:raise ValueError('RO object differs from ELF readonly LOAD')
  else:raise ValueError('unowned PT_LOAD range '+hex(p['start']))
 expected[-64:]=bytes([0xa7])*64
 table=[p for p in loads if p['start']==RO];assert len(table)==1 and metadata['runtime']['rodata_gpu']==RO and metadata['runtime']['rodata_bytes']==RO_BYTES
 data=pathlib.Path(wire).read_bytes()
 if data!=expected:raise ValueError('wire must equal actual code loads + zero padding + immutable A7 guard trailer')
 if metadata['runtime']['tls_base']!=end or metadata['runtime']['tbss_stride']!=stride or metadata['runtime']['crt_gpu']!=CRT or metadata['runtime']['crt_bytes']!=CRT_BYTES:raise ValueError('linked symbols differ from loader binding')
 return dict(scope='RV32_MOSAIC_STATIC_V2_OBJECT_OWNERSHIP_NOT_EXECUTION',code_gpu=CODE,code_bytes=CODE_BYTES,linked_code_end=linked_end,code_sha256=hashlib.sha256(data).hexdigest(),crt_gpu=CRT,crt_bytes=CRT_BYTES,bss_state_first=state_first,tls_base=end,tls_end=end+64*stride,tls_stride=stride,guard_trailer_bytes=64,rodata_gpu=RO,rodata_bytes=RO_BYTES,rodata_sha256=hashlib.sha256(pathlib.Path(rodata).read_bytes()).hexdigest(),gpu_raw_cycles=None)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('elf');p.add_argument('wire');p.add_argument('rodata');p.add_argument('metadata');p.add_argument('--out',required=True);a=p.parse_args();out=pathlib.Path(a.out);out.parent.mkdir(parents=True,exist_ok=True);rec=dict(scope='RV32_MOSAIC_STATIC_V2_OBJECT_OWNERSHIP_NOT_EXECUTION',natural_exit=1,gpu_raw_cycles=None)
 try:rec.update(audit(a.elf,a.wire,a.rodata,json.loads(pathlib.Path(a.metadata).read_text())),natural_exit=0)
 except Exception as e:rec['error']=repr(e)
 out.write_text(json.dumps(rec,indent=2)+'\n');print(json.dumps(rec));raise SystemExit(rec['natural_exit'])
