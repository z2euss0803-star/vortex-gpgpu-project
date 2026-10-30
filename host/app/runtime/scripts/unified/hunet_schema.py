"""Sealed schema of controlled HUNet0x10010; old program identities untouched."""
import struct
def make_schema():
 rules=[]
 def scalar(off,lo,hi,words=1):
  a=[0]*16;a[0]=1;a[1]=off;a[10]=words*4;a[11]=lo;a[12]=hi;rules.append(a)
 scalar(0,1,1);scalar(4,128,128);scalar(8,4,15);scalar(12,0,18)
 for i in range(8):
  a=[0]*16;a[:10]=[2,16+4*i,i,1 if i<6 else 2 if i==6 else 3,4,1,48+4*i,0xffffffff,0xffffffff,0xffffffff];rules.append(a)
 scalar(48,1,67108864,8);scalar(80,1,16384,2);scalar(88,1,4096,4);scalar(104,0,7);scalar(108,0,8);scalar(112,0,7);scalar(116,0,16384,2);scalar(124,0,0xffffffff)
 header=[0x534c5856,1,64+len(rules)*64,128,len(rules),0x80000000,0,0x10002000,4096,0xfff00000,0x100000,1,0,0,0,0]
 return struct.pack('<16I',*header)+b''.join(struct.pack('<16I',*r) for r in rules)
