#include <uapi/vx_linux.h>
#include <vxl/v2/types.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(struct vxl_request)==176,"legacy ABI");
_Static_assert(sizeof(struct vxl_compute_v2_request)==128,"request");
_Static_assert(offsetof(struct vxl_compute_v2_request,device_generation)==16,"epoch offset");
_Static_assert(offsetof(struct vxl_compute_v2_request,state)==88,"state offset");
_Static_assert(offsetof(struct vxl_compute_v2_request,reserved_tail)==96,"reserved offset");
_Static_assert(sizeof(struct vxl_v2_ref)==48,"ref");
_Static_assert(sizeof(struct vxl_v2_job)==512,"job");
_Static_assert(offsetof(struct vxl_v2_job,refs)==128,"job header");
_Static_assert(sizeof(struct vxl_v2_completion)==256,"completion");
_Static_assert(offsetof(struct vxl_v2_completion,submit_ns)==96,"timing offset");
_Static_assert(sizeof(struct vxl_v2_caps)==256,"caps");
_Static_assert(sizeof(struct vxl_v2_catalog_header)==64,"catalog header");
_Static_assert(sizeof(struct vxl_v2_catalog_entry)==512,"catalog entry");
int main(void){
 struct vxl_compute_v2_request r={.major=2,.size=128,.device_generation=0xffffffffffffffffULL,.user_ptr=0x100001234ULL};
 struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.program_handle=1,.program_generation=1,.device_generation=1,.execution_budget_ns=1,.args_handle=2,.args_generation=1,.args_bytes=64};
 struct vxl_request old={.abi_major=1,.struct_size=176,.handle=0x0102030405060708ULL};
 const unsigned char *b=(const unsigned char*)&old;
 assert(b[16]==8 && b[23]==1 && b[0]==1 && b[4]==176);
 assert(_IOC_NR(VXL_SUBMIT)==8 && _IOC_SIZE(VXL_SUBMIT)==176);
 assert(_IOC_NR(VXL_V2_CAPS)==0x80 && _IOC_NR(VXL_V2_FAULT_INFO)==0x8b && _IOC_SIZE(VXL_V2_SUBMIT_BATCH)==128);
 assert(_IOC_NR(VXL_V2_BUFFER_INFO)==0x82 && _IOC_NR(VXL_V2_BUFFER_RELEASE)==0x84 && _IOC_NR(VXL_V2_PROGRAM_LOAD)==0x85 && _IOC_NR(VXL_V2_PROGRAM_INFO)==0x86 && _IOC_NR(VXL_V2_COMPLETE_REAP)==0x8a);
 assert(vxl_v2_request_valid(&r,0,0)==0);
 r.flags=1;assert(vxl_v2_request_valid(&r,0,0)==-EINVAL);r.flags=0;
 assert(vxl_v2_request_valid(&r,0,1)==-EOPNOTSUPP);
 r.major=1;assert(vxl_v2_request_valid(&r,0,0)==-EINVAL);r.major=2;
 r.size=127;assert(vxl_v2_request_valid(&r,0,0)==-EINVAL);r.size=128;
 r.reserved=1;assert(vxl_v2_request_valid(&r,0,0)==-EINVAL);r.reserved=0;
 for(unsigned int i=0;i<8;i++){r.reserved_tail[i]=1;assert(vxl_v2_request_valid(&r,0,0)==-EINVAL);r.reserved_tail[i]=0;}
 r.offset=~0ULL;r.length=2;assert(vxl_v2_request_valid(&r,0,0)==-EOVERFLOW);r.offset=0;r.length=0;
 assert(vxl_v2_job_valid(&j)==0);j.execution_budget_ns=0;assert(vxl_v2_job_valid(&j)==-EINVAL);
 j.execution_budget_ns=60000000000ULL;assert(vxl_v2_job_valid(&j)==0);j.execution_budget_ns++;assert(vxl_v2_job_valid(&j)==-EINVAL);j.execution_budget_ns=1;
 j.num_refs=8;for(unsigned i=0;i<8;i++)j.refs[i]=(struct vxl_v2_ref){.handle=i+3,.object_generation=1,.length=64,.access=1,.domain=2,.device_generation=1};
 assert(vxl_v2_job_valid(&j)==0);j.num_refs=9;assert(vxl_v2_job_valid(&j)==-EINVAL);j.num_refs=8;
 j.refs[7].device_generation=2;assert(vxl_v2_job_valid(&j)==-ESTALE);j.refs[7].device_generation=1;
 j.refs[0].access=4;assert(vxl_v2_job_valid(&j)==-EINVAL);j.refs[0].access=1;
 j.reserved[7]=1;assert(vxl_v2_job_valid(&j)==-EINVAL);j.reserved[7]=0;
 puts("V2_WIRE_LAYOUT_AND_REJECTION_PASS_NOT_BOARD");return 0;
}
