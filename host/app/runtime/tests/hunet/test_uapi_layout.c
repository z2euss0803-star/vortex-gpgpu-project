#include <vxl/core.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <vxl/hunet.h>
_Static_assert(sizeof(struct vxl_request)==176,"request");
_Static_assert(sizeof(struct vxl_hunet_job_v1)==128,"job");
_Static_assert(sizeof(struct vxl_hunet_matrix_v1)==32,"matrix");
_Static_assert(sizeof(struct vxl_hunet_seal_v1)==128,"seal");
_Static_assert(sizeof(struct vxl_hunet_begin_v1)==128,"begin");
_Static_assert(sizeof(struct vxl_hunet_end_v1)==128,"end");
_Static_assert(sizeof(struct vxl_hunet_binding_v1)==256,"binding");
_Static_assert(sizeof(struct vxl_hunet_job_evidence_v1)==256,"evidence");
_Static_assert(offsetof(struct vxl_hunet_job_v1,x)==32,"job pointers");
_Static_assert(offsetof(struct vxl_hunet_job_v1,ci)==64,"job dims");
_Static_assert(offsetof(struct vxl_hunet_job_v1,stride)==96,"job stride");
_Static_assert(offsetof(struct vxl_hunet_job_evidence_v1,args_sha256)==160,"args sha");
_Static_assert(offsetof(struct vxl_hunet_job_evidence_v1,execution_deadline_ns)==192,"deadline");
_Static_assert(offsetof(struct vxl_hunet_job_evidence_v1,owner_counts)==212,"owners");
int main(int argc,char **argv) {
 struct vxl_request r={0};unsigned n;
 FILE *golden;
 struct vxl_hunet_seal_v1 s={0};struct vxl_hunet_begin_v1 b={0};
 r.abi_major=1;r.struct_size=176;
 for(n=1;n<=16;n++)assert(vxl_uapi_validate(_IOC(_IOC_READ|_IOC_WRITE,'V',n,176),&r,176,0)==0);
 r.flags=1;assert(vxl_uapi_validate(_IOC(_IOC_READ|_IOC_WRITE,'V',12,176),&r,176,0)!=0);r.flags=0;
 assert(vxl_uapi_validate(_IOC(_IOC_READ|_IOC_WRITE,'V',12,175),&r,176,0)!=0);
 assert(vxl_uapi_validate(_IOC(_IOC_READ|_IOC_WRITE,'V',12,176),&r,176,1)==-EOPNOTSUPP);
 s.magic=VXL_HUNET_SEAL_MAGIC;s.version=1;s.bytes=128;
 assert(vxl_hunet_payload_validate(VXL_HUNET_SEAL,&s,128)==0);
 s.reserved[47]=1;assert(vxl_hunet_payload_validate(VXL_HUNET_SEAL,&s,128)==-EINVAL);s.reserved[47]=0;
 s.flags=1;assert(vxl_hunet_payload_validate(VXL_HUNET_SEAL,&s,128)==-EINVAL);s.flags=0;
 s.magic++;assert(vxl_hunet_payload_validate(VXL_HUNET_SEAL,&s,128)==-EINVAL);s.magic--;
 assert(vxl_hunet_payload_validate(VXL_HUNET_SEAL,&s,127)==-EINVAL);
 b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.run_id[0]=1;
 assert(vxl_hunet_payload_validate(VXL_HUNET_BEGIN_ROI,&b,128)==0);
 b.reserved0=1;assert(vxl_hunet_payload_validate(VXL_HUNET_BEGIN_ROI,&b,128)==-EINVAL);
 assert(argc==2);s.code=0x1122334455667788ULL;s.weights=2;s.workspace=3;s.args=4;s.binding_sha256[0]=0xa5;
 golden=fopen(argv[1],"wb");assert(golden);assert(fwrite(&s,1,sizeof(s),golden)==sizeof(s));assert(fclose(golden)==0);
 puts("PASS_OFFLINE_UAPI_1_TO_16_REJECT_MALFORMED");return 0;
}
