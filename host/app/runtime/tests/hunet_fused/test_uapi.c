#include <vxl/hunet_fused.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(struct vxl_request)==176,"outer ABI must remain 176");
_Static_assert(sizeof(HunetFusedStageV1)==128,"stage");
_Static_assert(sizeof(HunetFusedDescriptorV1)==64,"descriptor");
_Static_assert(sizeof(struct vxl_fused_binding_v3)==512,"binding");
_Static_assert(offsetof(HunetFusedStageV1,stage_id)==24,"stage word6");
_Static_assert(offsetof(HunetFusedStageV1,policy)==120,"policy word30");
_Static_assert(offsetof(struct vxl_fused_binding_v3,fusion_contract_sha256)==384,"v2 prefix");
static void valid_binding(struct vxl_fused_binding_v3 *b){
 memset(b,0,sizeof(*b));
 b->v2.v1.magic=VXL_HUNET_BINDING_MAGIC;b->v2.v1.version=3;b->v2.v1.bytes=512;
 b->v2.v1.profile_id=4;b->v2.v1.program_id=9;b->v2.v1.features=15;
 memset(b->v2.v1.profile_sha256,0x11,224);
 memset(b->v2.cpu_binary_sha256,0x22,96);
 memset(b->fusion_contract_sha256,0x33,96);
}
int main(int argc,char **argv){
 struct vxl_fused_binding_v3 b;
 HunetFusedStageV1 stage={.magic=0x53554648,.abi=1,.bytes=128,.op=15,
 .first_logical_sequence=1,.last_logical_sequence=96,.stage_id=0,.descriptor_count=32,
 .input_gpu=0x85000040,.weights_gpu=0x80800000,.output_gpu=0x850300c0,
 .packed_b_gpu=0x86c4df40,.packed_b_bytes=131072,.stage_descriptors_gpu=0x86d0df80,
 .owner_trace_gpu=0x86d12e40,.pack_grid_rows=512,.gemm_grid_tiles=3072,
 .ci=3,.co=24,.ih=128,.iw=128,.oh=64,.ow=64,.kernel_k=2,.stride=2,
 .owner_trace_elements=3072,.dtype=1,.policy=1};
 HunetFusedDescriptorV1 d={.m=24,.n=4096,.k=12,.mp=24,.np=128,.kp=16,
 .offset=0,.count=128,.packed_b_offset=0,.pack_grid_base=0,.gemm_grid_base=0,
 .original_first_sequence=1,.original_last_sequence=3};
 assert(argc==2);valid_binding(&b);
 assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==0);
 assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,384)==-EINVAL);
 b.v2.v1.program_id=8;assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==-EINVAL);b.v2.v1.program_id=9;
 b.v2.v1.features=7;assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==-EINVAL);b.v2.v1.features=15;
 b.v2.v1.version=2;assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==-EINVAL);b.v2.v1.version=3;
 b.reserved[31]=1;assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==-EINVAL);b.reserved[31]=0;
 b.v2.reserved[0]=1;assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==-EINVAL);b.v2.reserved[0]=0;
 memset(b.layout_sha256,0,32);assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512)==-EINVAL);
 assert(vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,NULL,512)==-EFAULT);
 assert(vxl_fused_payload_validate(0,&b,512)==-ENOTTY);valid_binding(&b);
 FILE *f=fopen(argv[1],"wb");assert(f);
 assert(fwrite(&stage,1,128,f)==128 && fwrite(&d,1,64,f)==64 && fwrite(&b,1,512,f)==512);
 assert(fclose(f)==0);
 puts("FUSED_FIXED_ABI_AND_CROSS_MODE_REJECTION_PASS_NOT_BOARD");return 0;
}
