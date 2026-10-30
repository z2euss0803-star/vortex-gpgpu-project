#include <vxl/mosaic_layout.h>
int mosaic_layout_check(const struct mosaic_layout *layout){
 mosaic_u64 spatial,pow2=1,sizes[MOSAIC_ROLES],cursor=0x82000000ULL;unsigned i;
 if(!layout || layout->width<8 || layout->width>1280 || layout->height<8 || layout->height>960 || layout->width%4 || layout->height%4)return -EINVAL;
 spatial=(mosaic_u64)layout->width*layout->height;
 while(pow2<spatial)pow2*=2;
 sizes[0]=8ULL*1024*1024;sizes[1]=16ULL*1024*1024;
 sizes[2]=sizes[3]=spatial*32*4;sizes[4]=spatial*32*2;
 sizes[5]=spatial*32;sizes[6]=pow2*8*4;sizes[7]=128*3*4;
 sizes[8]=((2ULL*8*2592*2+64*8+384+32+63)/64*64)*4;
 for(i=0;i<MOSAIC_ROLES;++i){
  const struct mosaic_region *r=&layout->region[i];
  mosaic_u64 bytes=(sizes[i]+63)/64*64;
  if(r->bytes!=bytes)return -ERANGE;
  if(i<2){if(r->address!=(i?0x80800000ULL:0x80000000ULL)||r->guard||r->readonly!=1)return -EINVAL;}
  else{
   if(r->address!=cursor+64 || r->guard!=64 || r->readonly || r->address>=0xfff00000ULL || bytes>0xfff00000ULL-r->address-64)return -ERANGE;
   cursor=r->address+bytes+64;
  }
 }
 return cursor==layout->end?0:-EINVAL;
}
