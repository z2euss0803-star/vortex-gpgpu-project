#include <sys/ioctl.h>
#include <assert.h>
#include <stdio.h>
#include <vxl/hunet_opt.h>
#include <vxl/hunet_fused.h>
int main(void) {
 vxl_u32 job[32]={0x32474c48,2,128,16,1,2,7,8,1,0x84000000,320,0x86c4df40,4096,0,0,2};
 vxl_u32 desc[80]={0x32444748,2,320,1,1,2,7,8,2,2,0x86c4df40,4096};
 struct vxl_opt_binding_v4 b;
 struct vxl_hunet_binding_v1 *v=&b.v3.v2.v1;
 assert(sizeof(b)==512 && sizeof(struct vxl_opt_graph_job_v2)==128);
 assert(sizeof(struct vxl_request)==176 && sizeof(struct vxl_hybrid_cpu_evidence_v1)==384);
 assert(sizeof(struct vxl_opt_graph_header_v2)==128 && sizeof(struct vxl_opt_graph_node_v2)==96);
 memset(&b,0x11,sizeof(b)); memset(b.v3.v2.reserved,0,32);memset(b.v3.reserved,0,32);
 v->magic=VXL_HUNET_BINDING_MAGIC;v->version=4;v->bytes=512;v->profile_id=4;v->program_id=10;v->features=31;
 assert(vxl_opt_payload_validate(VXL_HYBRID_GET_BINDING,&b,sizeof(b))==0);
 assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,sizeof(b))!=0);
 v->version=3;assert(vxl_opt_payload_validate(VXL_HYBRID_GET_BINDING,&b,sizeof(b))!=0);
 v->version=4;b.v3.reserved[0]=1;assert(vxl_opt_payload_validate(VXL_HYBRID_GET_BINDING,&b,sizeof(b))!=0);
 b.v3.reserved[0]=0;v->features=15;assert(vxl_opt_payload_validate(VXL_HYBRID_GET_BINDING,&b,sizeof(b))!=0);
 desc[32]=4;desc[33]=7;desc[34]=desc[35]=1;desc[36]=0x85000040;desc[38]=0x85000200;
 desc[56]=5;desc[57]=8;desc[58]=desc[59]=2;desc[60]=0x85000040;desc[62]=0x85000300;
 assert(vxl_opt_graph_validate(job,sizeof(job),desc,sizeof(desc))==0);
 job[16]=1;assert(vxl_opt_graph_validate(job,sizeof(job),desc,sizeof(desc))!=0);job[16]=0;
 desc[32]=6;assert(vxl_opt_graph_validate(job,sizeof(job),desc,sizeof(desc))!=0);desc[32]=4;
 desc[36]=0xfffffff0;assert(vxl_opt_graph_validate(job,sizeof(job),desc,sizeof(desc))!=0);desc[36]=0x85000040;
 desc[58]=1;assert(vxl_opt_graph_validate(job,sizeof(job),desc,sizeof(desc))!=0);
 puts("OPT_ABI_AND_CROSS_VERSION_REJECTION_PASS_HOST_ONLY");return 0;
}
