#ifndef VXL_PROVIDER_COMMON_H
#define VXL_PROVIDER_COMMON_H
/* These are frozen delivery anchors, not a claim of measuring the running PL. */
static int digest_is(const vxl_u8 bytes[32],const char *hex){
 const char *digits="0123456789abcdef";unsigned int i;
 for(i=0;i<32;i++)if(digits[bytes[i]>>4]!=hex[2*i] || digits[bytes[i]&15]!=hex[2*i+1])return 0;
 return 1;
}
static int fixed_admit(const struct vxl_v2_boot_identity *b,const struct vxl_v2_catalog_entry *e,vxl_u32 flags,const char *compatible,const char *hardware,const char *bit,const char *xsa){
 if(!b || !e)return -EINVAL;
 if(b->provider.flags!=flags || !memchr(b->compatible,0,64) || strcmp(b->compatible,compatible) || !digest_is(b->provider.hardware_contract,hardware) || !digest_is(b->provider.bit,bit) || !digest_is(b->provider.xsa,xsa))return -EXDEV;
 if(e->provider_flags!=flags || memcmp(e->hardware_contract,b->provider.hardware_contract,32) || memcmp(e->bit,b->provider.bit,32) || memcmp(e->xsa,b->provider.xsa,32))return -EXDEV;
 if(!e->logical_profile || !e->logical_program)return -EINVAL;
 if(flags==4)return e->namespace_id>=1 && e->namespace_id<=3?0:-EXDEV;
 if(flags==1)return e->namespace_id==1 || e->namespace_id==3?0:-EXDEV;
 return e->namespace_id==2 && e->logical_profile==5 && e->logical_program>=9 && e->logical_program<=12?0:-EXDEV;
}
#endif
