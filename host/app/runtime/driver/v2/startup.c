#include <vxl/v2/startup.h>
#include <vxl/hunet.h>
int vxl_v2_boot_bind(struct vxl_v2_execution *x,struct vxl_v2_hw *h,struct vxl_v2_catalog *cat,const void *catalog,vxl_u32 n,const void *policy,vxl_u32 m,const vxl_u8 csha[32],const vxl_u8 psha[32]){
 struct vxl_v2_execution_header header;vxl_u8 digest[32];int rc;
 if(!x || !h || !cat || !catalog || !policy || !csha || !psha || n<64 || n>65536 || m<64 || m>4160)return -EINVAL;
 if(!h->ctx || h->ctx->owner || h->ctx->live || h->ctx->active)return -EBUSY;
 vxl_hunet_hash(catalog,n,digest);if(memcmp(digest,csha,32))return -EBADMSG;
 vxl_hunet_hash(policy,m,digest);if(memcmp(digest,psha,32))return -EBADMSG;
 memcpy(&header,policy,64);
 if(header.magic!=VXL_V2_EXEC_MAGIC || (header.version!=1 && header.version!=2) || header.bytes!=m || !header.count || header.count>16 || m!=64+256*header.count)return -EINVAL;
 for(vxl_u32 i=0;i<4;i++)if(header.reserved[i])return -EINVAL;
 if(memcmp(header.catalog_sha,csha,32))return -EXDEV;
 if(header.version==1)for(vxl_u32 i=0;i<header.count;i++){
  struct vxl_v2_execution_policy p;memcpy(&p,(const vxl_u8 *)policy+64+256*i,256);
  if(p.reserved[0])return -EINVAL; /* old file version cannot opt into new mode */
 }
 rc=vxl_v2_catalog_open(cat,catalog,n,&h->identity.provider);if(rc)return rc;
 rc=vxl_v2_hw_idle(h);if(rc)return rc;
 if(h->read(h->os,0xc)!=0 || h->read(h->os,0x30)!=15 || h->read(h->os,0x38)!=0x10001)return -ENODEV;
 if((h->identity.provider.flags==2 || h->identity.provider.flags==4) && (h->read(h->os,0x60)!=0x41584931 || h->read(h->os,0x64)!=1 || h->read(h->os,0x68)!=4))return -ENODEV;
 return vxl_v2_execution_bind(x,h,cat,(const struct vxl_v2_execution_policy *)((const vxl_u8 *)policy+64),header.count);
}
