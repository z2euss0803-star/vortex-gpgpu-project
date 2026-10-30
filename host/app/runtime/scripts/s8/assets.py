"""Deterministic controlled inputs and guarded initial objects, no device IO."""
import hashlib,struct
from scripts.s8.oracle import positive_inputs
from scripts.s8.layout import initial_image,guard_byte
def payloads(case,slot):
 ident=case['id'];core,warp=divmod(slot,4)
 if ident==1:return b'',b''
 if ident==2:return struct.pack('<64I',*range(64)),struct.pack('<64I',*range(63,-1,-1))
 if ident==3:
  half=lambda x:0 if not x else 0x3c00 if x==1 else 0xbc00
  return struct.pack('<64H',*(half((i+k)%3-1) for i in range(8) for k in range(8))),struct.pack('<32H',*(half((k+j)%3-1) for k in range(8) for j in range(4)))
 if ident in (4,5):return struct.pack('<4I',0,0x04040404,0x08080808,0x0c0c0c0c),struct.pack('<10I',0,0,65536,0,0,65536,65536,65536,32768,32768)[:case['b_bytes']]
 if ident>>8>=201:
  a=[[(t+1)*(i+1)+slot for i in range(8)] for t in range(2)];b=[[(t+1)*(i+1)-slot for i in range(8)] for t in range(2)]
 else:a,b=positive_inputs(ident,core,warp)
 return tuple(struct.pack('<%dh'%(len(m)*8),*(v for row in m for v in row)) for m in (a,b))
def objects(case,flat):
 result=[]
 def add(gpu,purpose,data):result.append(dict(gpu=gpu,purpose=purpose,bytes=len(data),data=bytes(data)))
 code=bytearray(initial_image(1,0));code[:len(flat)]=flat;add(0x80000000,1,code);add(0x80100000,2,initial_image(2,0))
 inputs=[payloads(case,slot) for slot in range(16)]
 for which in range(2):
  for slot in range(16):
   payload=inputs[slot][which]
   if len(payload)!=case['b_bytes' if which else 'a_bytes']:raise ValueError('catalog input bytes differ')
   image=bytearray(initial_image(3+which,slot,len(payload)));image[64:64+len(payload)]=payload;add((0x81000000 if which==0 else 0x81200000)+slot*0x20000,3,image)
 for slot in range(16):add(0x81400000+slot*4096,3,initial_image(5,slot))
 add(0x81410000,3,initial_image(6,0));add(0x81411000,3,initial_image(7,0))
 stack=bytearray([0x6d])*0x80040
 for hart in range(64):stack[(63-hart)*8192:(63-hart)*8192+64]=bytes(guard_byte(8,hart,0,i) for i in range(64))
 stack[-64:]=bytes(guard_byte(8,0,1,i) for i in range(64));add(0xfff70000,3,stack)
 if len(result)!=53:raise ValueError('object count')
 return result
