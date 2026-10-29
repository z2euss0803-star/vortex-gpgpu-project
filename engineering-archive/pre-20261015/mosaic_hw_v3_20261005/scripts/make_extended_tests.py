"""Native RV32 diagnostics and dense CPU matrix/texture reference vectors."""
from pathlib import Path
import struct,json,random
r=Path(__file__).resolve().parents[1];out=r/'kernel';rng=random.Random(260926)
def imm(op,rd,f,rs,x):return ((x&4095)<<20)|(rs<<15)|(f<<12)|(rd<<7)|op
def reg(op,rd,f,a,b):return (b<<20)|(a<<15)|(f<<12)|(rd<<7)|op
def st(op,src,base,off):return ((off>>5)<<25)|(src<<20)|(base<<15)|(2<<12)|((off&31)<<7)|op
def kernel(fmt,masked=False):
 c=[]
 def e(w):c.append(w)
 e(imm(0x13,5,0,0,4));e(0x10000337);e(imm(0x13,6,0,6,16));e(reg(0xb,0,1,5,6))
 e(imm(0x13,5,0,0,15));e(reg(0xb,0,0,5,0));e(imm(0x73,7,2,0,0x340));e(imm(0x73,8,2,0,0xf14))
 e(imm(0x13,15,7,8,3));e(imm(0x13,15,1,15,2));e(imm(0x13,16,5,8,2))
 for rd,offset in [(9,0),(10,4),(11,8)]:e(imm(3,rd,2,7,offset))
 e(imm(0x13,17,1,16,7))
 for a in [9,11]:e(reg(0x33,a,0,a,17));e(reg(0x33,a,0,a,15))
 e(imm(0x13,17,1,16,6));e(reg(0x33,10,0,10,17));e(reg(0x33,10,0,10,15))
 for f in range(8):e(imm(7,f,2,9,16*f))
 for f in range(4):e(imm(7,28+f,2,10,16*f))
 for f in range(8):e(imm(7,10+f,2,7,24))
 e((2<<25)|(fmt<<15)|((8 if fmt==9 else 0)<<7)|0xb)
 # TEX and integer loads can overlap TCU execution before its dependent stores.
 for rd,off in [(9,12),(10,16),(12,20)]:e(imm(3,rd,2,7,off))
 e(imm(0x13,8,1,8,2))
 for rd in [9,10,12]:e(reg(0x33,rd,0,rd,8))
 e(imm(3,9,2,9,0));e(imm(3,10,2,10,0))
 e(imm(0x13,5,0,0,5 if masked else 15));e(reg(0xb,0,0,5,0))
 e(reg(0x2b,14,0,9,10));e(st(0x23,14,12,0))
 e(imm(0x13,5,0,0,15));e(reg(0xb,0,0,5,0))
 for f in range(8):e(st(0x27,10+f,11,16*f))
 e(0x0330000f);e(reg(0xb,0,0,0,0));e(0x6f)
 return c
def fbits(x):return struct.unpack('<I',struct.pack('<f',float(x)))[0]
def pack(vals,fmt):
 if fmt==9:return sum((int(v)&255)<<(i*8) for i,v in enumerate(vals))
 if fmt==2:return sum((fbits(v)>>16)<<(i*16) for i,v in enumerate(vals))
 return sum(struct.unpack('<H',struct.pack('<e',v))[0]<<(i*16) for i,v in enumerate(vals))
cases=[];sv=[]
for case in range(10):
 fmt=[9,2,1,1,9,2,1,9,2,1][case];ratio=4 if fmt==9 else 2;K=4*ratio
 code=kernel(fmt,case==8);(out/f'extended_{case}.hex').write_text('\n'.join(f'{x:08x}' for x in code)+'\n')
 (out/f'extended_{case}.bin').write_bytes(struct.pack('<'+'I'*len(code),*code))
 (out/f'extended_{case}.S').write_text('.section .text.start,"ax"\n.globl _start\n_start:\n'+''.join(f' .word 0x{x:08x}\n' for x in code))
 image=[0]*65536;image[:len(code)]=code
 def put(addr,data):image[(addr-0x10000000)//4:(addr-0x10000000)//4+len(data)]=data
 Aaddr,Baddr,Caddr=0x10020000,0x10020400,0x10021000
 Uaddr,Vaddr,Taddr=0x10023000,0x10023100,0x10022000
 init=0 if case==3 else 0x7ffffff0 if case==7 else 1
 put(0x10010000,[Aaddr,Baddr,Caddr,Uaddr,Vaddr,Taddr,int(init) if fmt==9 else fbits(init)])
 expected=[]
 for warp in range(4):
  A=[[((i*3+k+warp)%7)-3 for k in range(K)] for i in range(8)]
  B=[[((k+j*2+warp)%5)-2 for j in range(4)] for k in range(K)]
  if case in (4,7):
   edge=[-128,127,-1,0,1]
   A=[[edge[(i+k+warp)%5] for k in range(K)] for i in range(8)]
   B=[[edge[(k+2*j+warp)%5] for j in range(4)] for k in range(K)]
  if case==6:
   A=[[(-1 if (i+k+warp)%2 else 1)*65504 for k in range(K)] for i in range(8)]
   B=[[(-1 if (k+j+warp)%2 else 1)/32 for j in range(4)] for k in range(K)]
  if case==3:A=[[2**-24 for k in range(K)] for i in range(8)];B=[[1 for j in range(4)] for k in range(K)]
  av=[];bv=[];cv=[]
  for rr in range(8):
   for lane in range(4):
    row=(rr//2)*2+lane//2;kw=((rr%2)*2+lane%2)*ratio
    av.append(pack(A[row][kw:kw+ratio],fmt))
  for rr in range(4):
   for lane in range(4):
    col=(rr%2)*2+lane//2;kw=((rr//2)*2+lane%2)*ratio
    bv.append(pack([B[k][col] for k in range(kw,kw+ratio)],fmt))
  for rr in range(8):
   for lane in range(4):
    row=(rr//2)*2+lane//2;col=(rr%2)*2+lane%2
    answer=init+sum(A[row][k]*B[k][col] for k in range(K))
    cv.append(int(answer)&0xffffffff if fmt==9 else fbits(answer))
  put(Aaddr+128*warp,av);put(Baddr+64*warp,bv);expected.extend(cv)
 put(Caddr,[0xdeadbeef]*128);put(Caddr-64,[0x55aa55aa]*16);put(Caddr+512,[0xaa55aa55]*16)
 mode=8+(case%8) if case!=9 else 0
 coords=[(-32768,-32768),(-1,-65537),(0,0),(32768,32768),(2147483647,-2147483648)]+[(rng.randrange(-400000,800000),rng.randrange(-400000,600000)) for _ in range(11)]
 put(Uaddr,[a&0xffffffff for a,b in coords]);put(Vaddr,[b&0xffffffff for a,b in coords])
 texture=[rng.getrandbits(32) for _ in range(40)];put(0x10024000,texture)
 put(Taddr,[0xdeadbeef]*16);put(Taddr-64,[0x12345678]*16);put(Taddr+64,[0x87654321]*16)
 texgold=[]
 for u,v in coords:
  def tex(x,y):
   x=x%7 if mode&2 else max(0,min(6,x));y=y%5 if mode&4 else max(0,min(4,y));return texture[y*8+x]
  if not mode&8:ans=0
  elif not mode&1:ans=tex((u+32768)//65536,(v+32768)//65536)
  else:
   x=u//65536;y=v//65536;fx=u%65536//256;fy=v%65536//256
   p=[tex(x,y),tex(x+1,y),tex(x,y+1),tex(x+1,y+1)];w=[(256-fx)*(256-fy),fx*(256-fy),(256-fx)*fy,fx*fy]
   ans=sum(((sum(((q>>(c*8))&255)*t for q,t in zip(p,w))+32768)>>16)<<(8*c) for c in range(4))
  texgold.append(0xdeadbeef if case==8 and len(texgold)%2 else ans)
 (out/f'image_{case}.hex').write_text('\n'.join(f'{x:08x}' for x in image)+'\n')
 (out/f'gold_{case}.hex').write_text('\n'.join(f'{x:08x}' for x in expected+texgold)+'\n')
 cases.append(dict(case=case,fmt=fmt,mode=mode,kernel_words=len(code),tcu_results=128,texture_results=16,expected_error=16 if mode==0 else 0))
(out/'extended_cases.json').write_text(json.dumps(cases,indent=2))
print('EXTENDED_VECTORS_READY',len(cases),'dense matrices and native TEX')
