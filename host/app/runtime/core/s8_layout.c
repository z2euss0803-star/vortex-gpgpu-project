#include <vxl/s8.h>
int vxl_s8_layout_check(const struct vxl_s8_layout *l) {
 static const struct vxl_s8_region fixed[8]={
  {0x80000000ULL,0x100000},{0x80100000ULL,0x1000},{0x81000000ULL,0x200000},{0x81200000ULL,0x200000},
  {0x81400000ULL,0x10000},{0x81410000ULL,0x1000},{0x81411000ULL,0x1000},{0xfff70000ULL,0x80040}};
 vxl_u64 end;
 if(!l)return -EINVAL;
 for(unsigned i=0;i<8;++i)if(l->regions[i].begin!=fixed[i].begin || l->regions[i].size!=fixed[i].size)return -EINVAL;
 if(!l->runtime_bytes || l->runtime_bytes>0xf0000 || l->objects_count!=53 || l->tdata_bytes || !l->static_stack_bytes || l->static_stack_bytes>=8128)return -EINVAL;
 if(!l->tbss_bytes || l->tbss_stride<l->tbss_bytes || !l->tbss_align || (l->tbss_align&(l->tbss_align-1)) || l->tbss_stride%l->tbss_align || l->tls_base%l->tbss_align)return -EINVAL;
 if(l->tls_base<0x80000000ULL || l->tls_base>0x800f0000ULL)return -EINVAL;
 end=l->tls_base+64ULL*l->tbss_stride;
 if(end>0x80000000ULL+l->runtime_bytes)return -EINVAL;
 return 0;
}
vxl_u32 vxl_s8_guard_byte(vxl_u32 region,vxl_u32 slot,vxl_u32 index,vxl_u32 j) {
 return (0xa5u^region^slot^(index*0x3du)^j)&255u;
}
int vxl_s8_guard_check(const vxl_u8 *p,vxl_u32 region,vxl_u32 slot,vxl_u32 index) {
 vxl_u32 slots=region==8?64:((region==3 || region==4 || region==5)?16:1);
 if(!p || region<1 || region>9 || slot>=slots || index>2)return -EINVAL;
 for(vxl_u32 j=0;j<64;++j)if(p[j]!=vxl_s8_guard_byte(region,slot,index,j))return -EIO;
 return 0;
}
int vxl_s8_gpu_bus(vxl_u32 gpu,vxl_u64 length,vxl_u64 *bus) {
 if(!bus || gpu<0x80000000u || !length || length>0x100000000ULL-gpu)return -EINVAL;
 *bus=VXL_PL_BASE+(gpu&0x7fffffffu);return 0;
}
int vxl_s8_ps_user_range(vxl_u64 bus,vxl_u64 length) {
 static const struct vxl_s8_region reserved[3]={{0x10002000,0x1000},{0x10030000,64},{0x100ff000,0x1000}};
 if(bus<VXL_PS_BASE || bus>=VXL_PS_BASE+VXL_PS_SIZE || !length || length>VXL_PS_BASE+VXL_PS_SIZE-bus)return -EINVAL;
 for(unsigned i=0;i<3;++i)if(bus<reserved[i].begin+reserved[i].size && reserved[i].begin<bus+length)return -EINVAL;
 return 0;
}
int vxl_s8_check_unchanged(const vxl_u8 *a,const vxl_u8 *b,vxl_u32 bytes,const struct vxl_s8_mutable *m,vxl_u32 count) {
 vxl_u32 cursor=0;
 if(!a || !b || (!m && count) || count>64)return -EINVAL;
 for(vxl_u32 i=0;i<count;++i) {
  if(m[i].offset<cursor || m[i].offset>bytes || !m[i].length || m[i].length>bytes-m[i].offset)return -EINVAL;
  if(memcmp(a+cursor,b+cursor,m[i].offset-cursor))return -EIO;
  cursor=m[i].offset+m[i].length;
 }
 return memcmp(a+cursor,b+cursor,bytes-cursor)?-EIO:0;
}
