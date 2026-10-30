#include <vxl/mosaic.h>
static mosaic_u32 get32(const mosaic_u8 *p){return (mosaic_u32)p[0]|((mosaic_u32)p[1]<<8)|((mosaic_u32)p[2]<<16)|((mosaic_u32)p[3]<<24);}
mosaic_u32 mosaic_job_crc(const struct mosaic_job_v1 *job){
 const mosaic_u8 *p=(const mosaic_u8*)job;mosaic_u32 crc=~0u;unsigned i,j;
 if(!job)return 0;
 for(i=0;i<128;++i){crc^=(i>=104&&i<108)?0:p[i];for(j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
 return ~crc;
}
int mosaic_validate_job(struct mosaic_ctx *ctx,const struct mosaic_job_v1 *job){
 const mosaic_u8 *p=(const mosaic_u8*)job,*e;mosaic_u32 first,count;unsigned i,nonzero=0;
 if(!ctx||!job||mosaic_layout_check(ctx->layout))return -EINVAL;
 e=(const mosaic_u8*)&ctx->expected;
 if(get32(p)!=0x31434d56u || p[4]!=1 || p[5]!=0 || p[6]!=128 || p[7]!=0)return -EINVAL;
 if(get32(p+104)!=mosaic_job_crc(job))return -EINVAL;
 for(i=112;i<128;++i)if(p[i])return -EINVAL;
 for(i=8;i<24;++i)nonzero|=p[i];
 if(!nonzero||!get32(p+40)||!get32(p+68))return -EINVAL;
 if(get32(p+64)!=15||get32(p+52)>=16||get32(p+48)<1||get32(p+48)>6||(get32(p+108)&~1u))return -EINVAL;
 /* Every ordered identity field is frozen by the trusted graph owner.
  * count may be a bounded prefix; flags bit0 only changes diagnostics. */
 if(memcmp(p+8,e+8,52)||memcmp(p+64,e+64,40))return -ESTALE;
 first=get32(p+56);count=get32(p+60);
 if(first>=ctx->total_tiles || !count || count>ctx->total_tiles-first)return -ERANGE;
 return 0;
}
