#include "ops.h"
#include "ranges.h"
#include <errno.h>
int vxnn_i16_matmul(const int16_t*a,size_t ac,const int16_t*b,size_t bc,uint32_t m,uint32_t n,uint32_t k,int64_t*out,size_t oc,const mosaic_s8_ops*io){
 using namespace vxnn_device;
 if(!matrix(m,n)||!k||k>64||ac<size_t(m)*k||bc<size_t(k)*n||oc<size_t(m)*n||!io||io->core>3||io->warp||io->participant>4||!io->begin||!io->step||!io->read||!io->release||!io->fence||!io->now||!io->poll_limit||io->poll_limit>65536)return -EINVAL;
 const size_t ab=size_t(m)*k*2,bb=size_t(k)*n*2,ob=size_t(m)*n*8;
 if(!span(a,ab,2)||!span(b,bb,2)||!span(out,ob,8)||overlap(a,ab,out,ob)||overlap(b,bb,out,ob))return -EINVAL;
 int16_t av[64*8],bv[64*8];int64_t tile[64];const uint32_t cols=n/8,tiles=(m/8)*cols;
 for(uint32_t id=io->core;id<tiles;id+=4){
  const uint32_t row=(id/cols)*8,col=(id%cols)*8;
  for(uint32_t t=0;t<k;t++)for(uint32_t lane=0;lane<8;lane++){av[t*8+lane]=a[(row+lane)*k+t];bv[t*8+lane]=b[t*n+col+lane];}
  int rc=mosaic_tile8(av,bv,k,tile,io);if(rc)return rc;
  // Four lanes participate in PE instructions; only the owner publishes.
  if(io->participant==0||io->participant==4)for(uint32_t r=0;r<8;r++)for(uint32_t c=0;c<8;c++)out[(row+r)*n+col+c]=tile[r*8+c];
 }
 return 0;
}
