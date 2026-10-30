#include <vxl/core.h>
#include <vxl/hunet.h>
#include <stddef.h>
int vxl_range_check(vxl_u64 cap,vxl_u64 off,vxl_u64 len) {
 if (!len) return -EINVAL;
 if (len > (~(vxl_u64)0)-off) return -EOVERFLOW;
 return off>cap || len>cap-off ? -EINVAL : 0;
}
int vxl_addr_encode(const struct vxl_profile *p,vxl_u32 domain,vxl_u64 bus,vxl_u64 len,vxl_u32 *gpu) {
 vxl_u64 base,cap,off,g;
 if (!p||!gpu) return -EINVAL;
 if (domain==VXL_PS) {base=p->ps_base;cap=p->ps_size;g=base;}
 else if (domain==VXL_PL) {base=p->pl_base;cap=p->pl_size;g=0x80000000ULL;}
 else return -EINVAL;
 if(bus<base) return -ERANGE;
 off=bus-base;
 if(vxl_range_check(cap,off,len)) return -ERANGE;
 if(domain==VXL_PS && bus<VXL_FENCE+64 && bus+len>VXL_FENCE) return -ERANGE;
 if(g+off>0xffffffffULL || len-1>0xffffffffULL-(g+off)) return -ERANGE;
 *gpu=(vxl_u32)(g+off); return 0;
}
int vxl_uapi_validate(vxl_u32 cmd,const struct vxl_request *r,vxl_u32 size,int compat) {
 if(compat) return -EOPNOTSUPP;
 if(!r) return -EFAULT;
 if(_IOC_TYPE(cmd)!='V' || _IOC_NR(cmd)<1 || _IOC_NR(cmd)>22 || _IOC_SIZE(cmd)!=sizeof(*r) || _IOC_DIR(cmd)!=(_IOC_READ|_IOC_WRITE)) return -ENOTTY;
 if(r->abi_major!=1) return -EPROTONOSUPPORT;
 if(size!=sizeof(*r)||r->struct_size!=sizeof(*r)||r->flags||r->reserved||r->reserved2) return -EINVAL;
 return 0;
}
static int nonzero(const vxl_u8 *p,size_t n) {size_t i;for(i=0;i<n;i++)if(p[i])return 1;return 0;}
int vxl_hunet_payload_validate(vxl_u32 cmd,const void *p,size_t n) {
 vxl_u32 h[4],magic,bytes;const vxl_u8 *b=p;
 switch(_IOC_NR(cmd)) {
 case 12:magic=VXL_HUNET_SEAL_MAGIC;bytes=128;break;
 case 13:magic=VXL_HUNET_BEGIN_MAGIC;bytes=128;break;
 case 14:magic=VXL_HUNET_END_MAGIC;bytes=128;break;
 case 15:magic=VXL_HUNET_EVIDENCE_MAGIC;bytes=256;break;
 case 16:magic=VXL_HUNET_BINDING_MAGIC;bytes=256;break;
 default:return -ENOTTY;
 }
 if(!p)return -EFAULT;
 if(n!=bytes)return -EINVAL;
 memcpy(h,p,sizeof(h));
 if(h[0]!=magic||h[1]!=1||h[2]!=bytes)return -EINVAL;
 if(_IOC_NR(cmd)!=16&&h[3])return -EINVAL;
 if(_IOC_NR(cmd)==12&&nonzero(b+80,48))return -EINVAL;
 if(_IOC_NR(cmd)==13&&(nonzero(b+44,4)||nonzero(b+96,32)||!nonzero(b+16,16)))return -EINVAL;
 if(_IOC_NR(cmd)==15&&(nonzero(b+156,4)||nonzero(b+228,28)))return -EINVAL;
 return 0;
}
int vxl_hybrid_payload_validate(vxl_u32 cmd,const void *p,size_t n) {
 vxl_u32 h[4],magic,bytes,version=1; const vxl_u8 *b=p;
 switch(cmd) {
 case VXL_HYBRID_SEAL: magic=VXL_HYBRID_SEAL_MAGIC;bytes=128;break;
 case VXL_HYBRID_CPU_BEGIN: magic=VXL_HYBRID_BEGIN_MAGIC;bytes=128;break;
 case VXL_HYBRID_CPU_COMMIT: magic=VXL_HYBRID_COMMIT_MAGIC;bytes=128;break;
 case VXL_HYBRID_GET_CPU_EVIDENCE: magic=VXL_HYBRID_EVIDENCE_MAGIC;bytes=384;break;
 case VXL_HYBRID_END: magic=VXL_HYBRID_END_MAGIC;bytes=256;break;
 case VXL_HYBRID_GET_BINDING: magic=VXL_HUNET_BINDING_MAGIC;bytes=384;version=2;break;
 default:return -ENOTTY;
 }
 if(!p)return -EFAULT;
 if(n!=bytes)return -EINVAL;
 memcpy(h,p,16);
 if(h[0]!=magic||h[1]!=version||h[2]!=bytes)return -EINVAL;
 if(cmd!=VXL_HYBRID_GET_BINDING&&h[3])return -EINVAL;
 if(cmd==VXL_HYBRID_SEAL&&nonzero(b+80,48))return -EINVAL;
 if(cmd==VXL_HYBRID_CPU_BEGIN&&nonzero(b+112,16))return -EINVAL;
 if(cmd==VXL_HYBRID_CPU_COMMIT) {
  const struct vxl_hybrid_commit_v1 *c=p;
  if(c->algorithm!=VXL_HYBRID_ERF_NATIVE_V1||c->threads!=4||nonzero(c->reserved,12))return -EINVAL;
 }
 if(cmd==VXL_HYBRID_GET_BINDING&&nonzero(b+352,32))return -EINVAL;
 if(cmd==VXL_HYBRID_GET_CPU_EVIDENCE&&nonzero(b+256,128))return -EINVAL;
 if(cmd==VXL_HYBRID_END&&nonzero(b+176,80))return -EINVAL;
 return 0;
}
int vxl_cdma_status(vxl_u32 sr) { if(sr&0x4770) return -EIO; return (sr&0x1002)==0x1002; }
int vxl_cdma_addresses(vxl_u64 a,vxl_u64 b,vxl_u64 len) {
 if(!len || len>4096 || ((a|b|len)&15)) return -EINVAL;
 if(a>=(1ULL<<40)||b>=(1ULL<<40)||len>(1ULL<<40)-a||len>(1ULL<<40)-b) return -ERANGE;
 return 0;
}
int vxl_probe_check(int res,int clk,int reset,int ready,vxl_u32 id,vxl_u32 config,int owner) {
 if(!res||!clk||!reset) return -ENODEV;
 if(owner) return -EBUSY;
 if(!ready) return -EAGAIN;
 return id!=0x56580204||config!=0x40404 ? -ENODEV : 0;
}
