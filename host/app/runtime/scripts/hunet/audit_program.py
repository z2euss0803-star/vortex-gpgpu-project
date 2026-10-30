"""Bounded ELF32/static instruction audit; no ISA, SIMT, RTL or device execution."""
import argparse,hashlib,json,struct
from pathlib import Path
def digest(b):return hashlib.sha256(b).hexdigest()
def elf_info(path):
 b=Path(path).read_bytes()
 if b[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('ELF_FORMAT')
 h=struct.unpack_from('<HHIIIIIHHHHHH',b,16)
 if h[0]!=2 or h[1]!=243 or h[3]!=0x80000000:raise ValueError('ELF_ENTRY_MACHINE')
 loads=[]
 for i in range(h[9]):
  t,o,v,p,f,m,flags,a=struct.unpack_from('<8I',b,h[4]+i*h[8])
  if t==1:
   if f>m or o+f>len(b) or v<0x80000000 or v+m>0x80800000:raise ValueError('ELF_SEGMENT_RANGE')
   loads.append(dict(offset=o,start=v,end=v+m,file_bytes=f,memory_bytes=m,flags=flags))
 sections=[struct.unpack_from('<10I',b,h[5]+i*h[10]) for i in range(h[11])]
 symbols={}
 for s in sections:
  if s[1]!=2:continue
  strings=sections[s[6]];st=b[strings[4]:strings[4]+strings[5]]
  for j in range(s[4],s[4]+s[5],s[9]):
   n,v,z,info,other,index=struct.unpack_from('<IIIBBH',b,j)
   name=st[n:].split(b'\0',1)[0].decode('utf-8',errors='strict')
   if name:symbols[name]=dict(value=v,size=z)
 return b,h,loads,symbols
def instructions(b,loads,symbols,name,max_words=200):
 address=symbols[name]['value']
 s=next(s for s in loads if s['start']<=address<s['start']+s['file_bytes'])
 offset=s['offset']+address-s['start'];available=(s['offset']+s['file_bytes']-offset)//4
 size=symbols[name]['size']//4
 if size:available=min(available,size)
 return list(struct.unpack_from('<%dI'%min(max_words,available),b,offset))
def audit_program(elf,wire,release):
 b,h,loads,symbols=elf_info(elf);raw=Path(wire).read_bytes()
 exit_words=instructions(b,loads,symbols,'_Exit',32)
 values={};core_stride=False;saved=False;store_reg=None;exit_base=None;perf_call=False
 for word in exit_words:
  op=word&127;rd=(word>>7)&31;rs1=(word>>15)&31;rs2=(word>>20)&31;fn=(word>>12)&7
  imm=(word>>20);imm=imm-4096 if imm&2048 else imm
  if op==0x37:values[rd]=word&0xfffff000
  elif op==0x13 and fn==0:
   if rs1 in values or rs1==0:values[rd]=(values.get(rs1,0)+imm)&0xffffffff
   if rd==8 and rs1==10 and imm==0:saved=True
  elif op in (0x6f,0x67) and rd==1:
   perf_call=True
   if not saved:raise ValueError('LOW_PS_IO: old CRT does not preserve real return')
  elif op==0x13 and fn==1 and rd==6 and rs1==6 and ((word>>20)&31)==8:core_stride=True
  elif op==0x33 and rd==5 and rs1==5 and rs2==6:pass
  elif op==0x23 and fn==2:
   simm=((word>>25)<<5)|((word>>7)&31);simm=simm-4096 if simm&2048 else simm
   exit_base=(values.get(rs1,0)+simm)&0xffffffff;store_reg=rs2;break
 if exit_base!=0x10002088:raise ValueError('LOW_PS_IO: exit is not private PS core slot')
 if not (saved and perf_call and core_stride and store_reg==8):raise ValueError('CRT_RETURN_CORE_STRIDE')
 if 0xcc202373 not in exit_words:raise ValueError('CRT_CORE_ID')
 for name,base in [('vx_putchar',0x10002040),('vx_perf_dump',0x10002080)]:
  words=instructions(b,loads,symbols,name)
  regs={};found=False
  for w in words:
   rd=(w>>7)&31;rs1=(w>>15)&31;op=w&127;fn=(w>>12)&7
   if op==0x37:regs[rd]=w&0xfffff000
   elif op==0x13 and fn==0:
    imm=w>>20;imm=imm-4096 if imm&2048 else imm
    if rs1 in regs or rs1==0:
     regs[rd]=(regs.get(rs1,0)+imm)&0xffffffff
     if regs[rd]==base:found=True
  if not found:raise ValueError('CRT_IO_ADDRESS:'+name)
 edata=symbols['_edata']['value'];end=symbols['_end']['value'];stride=symbols['__tbss_size']['value']
 if not (0x80000000<edata<=end<0x80800000) or not 0<stride<4096:raise ValueError('BSS_TLS_SYMBOLS')
 init=instructions(b,loads,symbols,'init_regs',24)
 if 0xffff0137 not in init or not any((w&0x707f)==0x1013 and (w>>20)&31==13 for w in init):raise ValueError('STACK_ABI')
 required=(max(end,max(s['end'] for s in loads))-0x80000000+63)&~63
 if len(raw)!=required:raise ValueError('WIRE_SEGMENT_LENGTH')
 expected=bytearray(required)
 for s in loads:
  off=s['start']-0x80000000;expected[off:off+s['file_bytes']]=b[s['offset']:s['offset']+s['file_bytes']]
 if raw!=expected:raise ValueError('WIRE_SEGMENT_CONTENT')
 tls_end=end+64*stride
 if tls_end>0x80800000:raise ValueError('TLS_RANGE')
 return dict(scope='STATIC_PROGRAM_AUDIT_NOT_EXECUTION',program_id=release['program_id'],entry=h[3],
   elf_sha256=digest(b),wire_sha256=digest(raw),wire_bytes=len(raw),segments=loads,
   immutable_spans=[dict(start=s['start'],bytes=s['file_bytes'],sha256=digest(b[s['offset']:s['offset']+s['file_bytes']])) for s in loads if not s['flags']&2],
   edata=edata,end=end,tls_stride=stride,tls_end=tls_end,stack_active_base=0xfff70000,
   stack_reserved_base=0xfff00000,stack_top=0xffff0000,stack_stride=8192,exit_register=store_reg,
   return_saved_before_perf=saved,exit_slots=[0x10002088+c*256 for c in range(4)])
def package_program(elf,out,program_id):
 b,h,loads,symbols=elf_info(elf);start=0x80000000;end=max(symbols['_end']['value'],max(s['end'] for s in loads))
 payload=bytearray(symbols['_edata']['value']-start)
 for s in loads:
  off=s['start']-start;payload[off:off+s['file_bytes']]=b[s['offset']:s['offset']+s['file_bytes']]
 (out/'kernel.vxbin').write_bytes(struct.pack('<QQ',start,end)+payload)
 wire=payload+bytearray(((end-start+63)&~63)-len(payload));(out/'code.bin').write_bytes(wire)
 report=audit_program(elf,out/'code.bin',dict(program_id=program_id))
 (out/'program.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--elf',required=True);p.add_argument('--out',required=True);p.add_argument('--program-id',type=int,required=True)
 a=p.parse_args();r=package_program(Path(a.elf),Path(a.out),a.program_id);print(json.dumps(r))
