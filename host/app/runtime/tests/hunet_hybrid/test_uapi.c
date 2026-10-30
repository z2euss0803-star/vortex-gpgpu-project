#include <vxl/core.h>
#include <uapi/vx_hunet_hybrid.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
int vxl_hybrid_payload_validate(vxl_u32,const void *,size_t);
_Static_assert(sizeof(struct vxl_hybrid_begin_v1)==128,"begin");
_Static_assert(sizeof(struct vxl_hybrid_commit_v1)==128,"commit");
_Static_assert(sizeof(struct vxl_hybrid_binding_v2)==384,"binding");
_Static_assert(sizeof(struct vxl_hybrid_cpu_evidence_v1)==384,"evidence");
_Static_assert(sizeof(struct vxl_hybrid_end_v1)==256,"end");
_Static_assert(offsetof(struct vxl_hybrid_cpu_evidence_v1,input_sha256)==80,"input sha");
_Static_assert(offsetof(struct vxl_hybrid_cpu_evidence_v1,read_ns)==176,"read time");
_Static_assert(offsetof(struct vxl_hybrid_cpu_evidence_v1,reserved)==256,"reserved");
_Static_assert(offsetof(struct vxl_hybrid_end_v1,input_sha256)==80,"end input");
int main(int argc,char **argv){
 struct vxl_request r={.abi_major=1,.struct_size=176};
 struct vxl_hybrid_begin_v1 b={.magic=VXL_HYBRID_BEGIN_MAGIC,.version=1,.bytes=128,
 .generation=2,.sequence=296,.stage=8,.dtype=1,.token=0x1122334455667788ULL,
 .input_ptr=0x2233445566778899ULL,.input_bytes=1572864,.output_bytes=1572864};
 struct vxl_hybrid_commit_v1 c={.magic=VXL_HYBRID_COMMIT_MAGIC,.version=1,.bytes=128,.algorithm=1,.threads=4};
 struct vxl_hybrid_binding_v2 bind={0};
 assert(argc==2);b.run_id[0]=10;b.input_sha256[0]=0xa5;
 assert(!vxl_uapi_validate(VXL_HYBRID_CPU_BEGIN,&r,176,0));
 assert(!vxl_uapi_validate(VXL_HYBRID_GET_BINDING,&r,176,0));
 assert(vxl_uapi_validate(_IOWR('V',23,struct vxl_request),&r,176,0)==-ENOTTY);
 assert(vxl_uapi_validate(VXL_HYBRID_CPU_BEGIN,&r,176,1)==-EOPNOTSUPP);
 assert(!vxl_hybrid_payload_validate(VXL_HYBRID_CPU_BEGIN,&b,128));
 assert(vxl_hybrid_payload_validate(VXL_HYBRID_CPU_BEGIN,NULL,128)==-EFAULT);
 assert(vxl_hybrid_payload_validate(VXL_HYBRID_CPU_BEGIN,&b,127)==-EINVAL);
 b.flags=1;assert(vxl_hybrid_payload_validate(VXL_HYBRID_CPU_BEGIN,&b,128)==-EINVAL);b.flags=0;
 b.reserved[0]=1;assert(vxl_hybrid_payload_validate(VXL_HYBRID_CPU_BEGIN,&b,128)==-EINVAL);b.reserved[0]=0;
 assert(!vxl_hybrid_payload_validate(VXL_HYBRID_CPU_COMMIT,&c,128));
 c.threads=2;assert(vxl_hybrid_payload_validate(VXL_HYBRID_CPU_COMMIT,&c,128)==-EINVAL);
 bind.v1.magic=VXL_HUNET_BINDING_MAGIC;bind.v1.version=1;bind.v1.bytes=256;
 assert(vxl_hybrid_payload_validate(VXL_HYBRID_GET_BINDING,&bind,384)==-EINVAL);
 FILE *f=fopen(argv[1],"wb");assert(f&&fwrite(&b,1,128,f)==128&&!fclose(f));
 puts("HYBRID_FIXED_ABI_AND_OLD_REJECTION_PASS_NOT_BOARD");return 0;
}
