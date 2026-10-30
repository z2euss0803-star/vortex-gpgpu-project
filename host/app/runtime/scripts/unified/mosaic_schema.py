"""Sealed schema of NEW Mosaic0x10020; no legacy program12 replacement."""
import struct
def make_schema(*,fused=False):
 rules=[]
 def scalar(offset,low,high,words=1):
  row=[0]*16;row[0]=1;row[1]=offset;row[10]=words*4;row[11]=low;row[12]=high;rules.append(row)
 scalar(0,1,2 if fused else 1);scalar(4,128,128);scalar(8,1,8 if fused else 6);scalar(12,0,2)
 access=(1,2,3,1,1,3,3,3);alignment=(4,4,2,2,4,4,4,64)
 for i in range(8):
  row=[0]*16;row[:10]=[2,16+4*i,i,access[i],alignment[i],1,48+4*i,0xffffffff,0xffffffff,0xffffffff];rules.append(row)
 scalar(48,1,0x7f700000,8);scalar(80,1,128,2);scalar(88,2,960);scalar(92,2,1280);scalar(96,3,9);scalar(100,1,2,2);scalar(108,32,2592);scalar(112,0,0xffffffff);scalar(116,1,4096);scalar(120,0,0x7f7fffff)
 zero=[0]*16;zero[0]=3;zero[1]=124;zero[10]=4;rules.append(zero)
 header=[0x534c5856,1,64+64*len(rules),128,len(rules),0x80000000,0x80110000,0x10002000,4096,0xfff00000,0x100000,1,0,0,0,0]
 assert len(rules)==23
 return struct.pack('<16I',*header)+b''.join(struct.pack('<16I',*row) for row in rules)
