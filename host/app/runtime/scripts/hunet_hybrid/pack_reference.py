"""Independent NCHW im2col layout + IEEE RNE FP32-to-FP16 PACK oracle.

Derived from the frozen job/descriptor semantics, without calling the GPU kernel.
"""
import math,struct
def pack_reference(j,m,input_bytes):
 if len(j)!=32 or len(m)!=8 or j[3]!=1 or j[5] not in [0,2]:raise ValueError('PACK_CONTRACT')
 ci,ih,iw,oh,ow,k,stride,pad=j[16],j[18],j[19],j[20],j[21],j[22],j[24],j[25]
 M,N,K,MP,NP,KP,offset,count=m
 if not min(ci,ih,iw,oh,ow,k,stride,NP,KP)>0 or len(input_bytes)!=ci*ih*iw*4 or j[7]!=KP*NP or K>KP or count>NP or offset+count>N:raise ValueError('PACK_BOUNDS')
 values=struct.unpack('<%df'%(ci*ih*iw),input_bytes)
 if not all(math.isfinite(x) for x in values):raise ValueError('PACK_INPUT_NONFINITE')
 out=bytearray(j[7]*2)
 for row in range(KP):
  channel=row//(k*k);ky=(row//k)%k;kx=row%k
  for column in range(NP):
   value=0.0
   if row<K and column<count:
    position=offset+column
    if j[5]==2:
     ix=position+row-pad
     if 0<=ix<iw:value=values[ix]
    else:
     y=(position//ow)*stride+ky-pad;x=(position%ow)*stride+kx-pad
     if 0<=y<ih and 0<=x<iw:
      index=channel*ih*iw+y*iw+x
      if index>=len(values):raise ValueError('PACK_CHANNEL_BOUNDS')
      value=values[index]
   struct.pack_into('<e',out,(row*NP+column)*2,value)
 return bytes(out)
