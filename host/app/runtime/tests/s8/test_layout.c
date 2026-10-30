#include <vxl/s8.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
 struct vxl_s8_layout l={.regions={{0x80000000ULL,0x100000},{0x80100000ULL,0x1000},{0x81000000ULL,0x200000},{0x81200000ULL,0x200000},{0x81400000ULL,0x10000},{0x81410000ULL,0x1000},{0x81411000ULL,0x1000},{0xfff70000ULL,0x80040}},.runtime_bytes=0x2400,.tls_base=0x80002000,.tbss_bytes=4,.tbss_stride=16,.tbss_align=16,.static_stack_bytes=4096,.objects_count=53};
 assert(vxl_s8_layout_check(&l)==0);
 struct vxl_s8_layout bad=l;bad.regions[2].begin++;assert(vxl_s8_layout_check(&bad)<0);
 bad=l;bad.static_stack_bytes=8128;assert(vxl_s8_layout_check(&bad)<0);
 bad=l;bad.tls_base=0x800efff0;assert(vxl_s8_layout_check(&bad)<0);
 bad=l;bad.tbss_stride=3;assert(vxl_s8_layout_check(&bad)<0);
 bad=l;bad.objects_count=65;assert(vxl_s8_layout_check(&bad)<0);
 vxl_u64 bus=0;assert(vxl_s8_gpu_bus(0x81000040,65536,&bus)==0 && bus==0x501000040ULL);
 assert(vxl_s8_gpu_bus(0x10000000,64,&bus)<0);
 assert(vxl_s8_gpu_bus(0xfffffff0,32,&bus)<0);
 assert(vxl_s8_ps_user_range(0x10002000,64)<0);assert(vxl_s8_ps_user_range(0x10001fc0,128)<0);
 assert(vxl_s8_ps_user_range(0x10030000,64)<0);assert(vxl_s8_ps_user_range(0x100ff000,64)<0);
 assert(vxl_s8_ps_user_range(0x10004000,4096)==0);
 vxl_u8 guard[64];for(unsigned j=0;j<64;++j)guard[j]=vxl_s8_guard_byte(3,5,1,j);
 assert(vxl_s8_guard_check(guard,3,5,1)==0);guard[7]^=1;assert(vxl_s8_guard_check(guard,3,5,1)<0);
 vxl_u8 a[128]={0},b[128]={0};struct vxl_s8_mutable m={64,32};b[64]=1;
 assert(vxl_s8_check_unchanged(a,b,128,&m,1)==0);b[63]=1;assert(vxl_s8_check_unchanged(a,b,128,&m,1)<0);
 puts("S8_LAYOUT_GUARD_DOMAINS_PASS objects=53");return 0;
}
