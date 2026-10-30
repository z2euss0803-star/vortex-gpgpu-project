#include <vxl/mosaic_assets.h>
#include <vxl/hunet.h>
static vxl_u32 le32(const vxl_u8 *p){return (vxl_u32)p[0]|(vxl_u32)p[1]<<8|(vxl_u32)p[2]<<16|(vxl_u32)p[3]<<24;}
static vxl_u64 le64(const vxl_u8 *p){return (vxl_u64)le32(p)|((vxl_u64)le32(p+4)<<32);}
static int zero(const vxl_u8 *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static int digest(const vxl_u8 *p,size_t n,const vxl_u8 expected[32]){vxl_u8 actual[32];vxl_hunet_hash(p,n,actual);return memcmp(actual,expected,32)?-EBADMSG:0;}
int mosaic_assets_validate(const void *bytes,size_t n,const struct mosaic_manifest *m){
 const vxl_u8 *p=bytes;size_t cursor=MOSAIC_ASSET_PAYLOAD;
 if(!p||!m)return -EINVAL;
 if(n<MOSAIC_ASSET_PAYLOAD||n>MOSAIC_ASSET_MAX||n%64)return -EOVERFLOW;
 if(memcmp(p,"MS8AST1",8)||le32(p+8)!=1||le32(p+12)!=256||le32(p+16)!=16||le32(p+20)!=384||le64(p+24)!=n||le64(p+32)!=MOSAIC_ASSET_PAYLOAD)return -EINVAL;
 if(!zero(p+40,8)||!zero(p+208,48))return -EINVAL;
 if(digest(p,n,m->package_sha)||digest(p+256,n-256,p+176))return -EBADMSG;
 for(unsigned i=0;i<4;i++)if(memcmp(p+48+32*i,m->source_sha[i],32))return -EBADMSG;
 for(unsigned i=0;i<16;i++){
  const struct mosaic_layer_shape *s=&m->layers[i];const vxl_u8 *record=p+256+i*384;
  vxl_u32 inner,kpad,segments=s->norm?5:3;
  if(!s->co||s->co>128||!s->ci||s->ci>128||(s->k!=3&&s->k!=9)||s->norm>1)return -EINVAL;
  inner=s->ci*s->k*s->k;if(inner>4096)return -ERANGE;kpad=(inner+31u)&~31u;
  if(le32(record)!=i||le32(record+4)!=s->co||le32(record+8)!=s->ci||le32(record+12)!=s->k||le32(record+16)!=inner||le32(record+20)!=kpad||le32(record+24)!=s->norm||le32(record+28)!=segments||!zero(record+32,32))return -EINVAL;
  for(unsigned j=0;j<segments;j++){
   const vxl_u8 *entry=record+64+j*64;vxl_u32 rows=j==2?1:s->co,cols=j==0?kpad:j==1?2:1;
   vxl_u64 offset=le64(entry+8),size=le64(entry+16),expected=(vxl_u64)rows*cols*(j?4:2);
   if(le32(entry)!=j+1||le32(entry+4)!=(j?2u:1u)||le32(entry+24)!=rows||le32(entry+28)!=cols||size!=expected)return -EINVAL;
   if(offset!=((cursor+63u)&~(size_t)63u)||offset>n||size>n-offset||!zero(p+cursor,(size_t)offset-cursor))return -EOVERFLOW;
   const vxl_u8 *raw=p+(size_t)offset;
   if(digest(raw,(size_t)size,entry+32))return -EBADMSG;
   if(!j){
    for(size_t z=0;z<(size_t)size;z+=2){
     vxl_u32 q=(vxl_u32)raw[z]|(vxl_u32)raw[z+1]<<8;
     if(q==0x8000u||((z/2)%kpad>=inner&&q))return -ERANGE;
    }
   }else{
    /* Integer bit checks avoid kernel floating point and work on any host endian. */
    for(size_t z=0;z<(size_t)size;z+=4){
     vxl_u32 bits=le32(raw+z);int scale=j==2||(j==1&&(z/4)%2==0);
     if((bits&0x7f800000u)==0x7f800000u||(scale&&((bits>>31)||!(bits&0x7fffffffu))))return -ERANGE;
    }
   }
   cursor=(size_t)(offset+size);
  }
  if(!zero(record+64+segments*64,(5-segments)*64))return -EINVAL;
 }
 if(((cursor+63u)&~(size_t)63u)!=n||!zero(p+cursor,n-cursor))return -EINVAL;
 return 0;
}
