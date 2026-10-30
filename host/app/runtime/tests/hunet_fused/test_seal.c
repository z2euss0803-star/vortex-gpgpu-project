#include "../hunet/memory_fixture.h"
#include "../../generated/hunet_fused/identity.h"
static void make_release(struct fixture*f,struct vxl_fused_release*r){
 memset(r,0,sizeof(*r));r->wire_bytes=f->r.program[0].wire_bytes;r->ro_bytes=f->r.program[0].ro_bytes;
 memcpy(r->wire_sha,f->r.program[0].wire_sha,32);memcpy(r->ro_sha,f->r.program[0].ro_sha,32);
 r->binding.v2.v1=f->r.binding;r->binding.v2.v1.version=3;r->binding.v2.v1.bytes=512;
 r->binding.v2.v1.program_id=9;r->binding.v2.v1.features=15;
 memcpy(r->binding.v2.v1.jobs_sha256,vxl_fused_jobs_sha256,32);memset(r->binding.v2.v1.source_sha256,0x81,32);
 memset(r->binding.v2.cpu_binary_sha256,1,32);memset(r->binding.v2.cpu_contract_sha256,2,32);memset(r->binding.v2.cpu_stages_sha256,3,32);
 memcpy(r->binding.fusion_contract_sha256,vxl_fused_contract_sha256,32);
 memcpy(r->binding.descriptors_sha256,vxl_fused_descriptors_sha256,32);
 memcpy(r->binding.layout_sha256,vxl_fused_layout_sha256,32);vxl_hunet_hash(&r->binding,512,r->binding_sha);
}
int main(void){
 struct fixture f;struct vxl_fused_release r,bad;struct vxl_hunet_seal_v1 s={0};struct vxl_hunet_begin_v1 begin={0};vxl_u8*input;
 (void)seal_fixture;init_fixture(&f);make_release(&f,&r);
 /* This fake TEST_ONLY release authorizes existing fixture bytes to test seal
    mechanics. It is never packaged, run as GPU program9, or used as RTL evidence. */
 assert(!vxl_fused_init(&f.c,&r));
 bad=r;bad.binding.v2.v1.weights_sha256[0]^=1;vxl_hunet_hash(&bad.binding,512,bad.binding_sha);assert(vxl_fused_init(&f.c,&bad));
 bad=r;bad.binding.layout_sha256[0]^=1;vxl_hunet_hash(&bad.binding,512,bad.binding_sha);assert(vxl_fused_init(&f.c,&bad));
 bad=r;bad.binding.v2.v1.program_sha256[0]^=1;vxl_hunet_hash(&bad.binding,512,bad.binding_sha);assert(vxl_fused_init(&f.c,&bad));
 assert(!vxl_fused_init(&f.c,&r));s.magic=VXL_HYBRID_SEAL_MAGIC;s.version=1;s.bytes=128;
 s.code=reserve_fixture(&f,VXL_CODE,VXL_HUNET_CODE,8388608);s.weights=reserve_fixture(&f,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864);
 s.workspace=reserve_fixture(&f,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES);s.args=reserve_fixture(&f,VXL_ARGS,0,128);
 memcpy(s.binding_sha256,r.binding_sha,32);s.binding_sha256[0]^=1;assert(vxl_fused_seal(&f.c,&s)==-ESTALE);assert(!f.c.sealed);
 s.binding_sha256[0]^=1;assert(!vxl_fused_seal(&f.c,&s));assert(f.c.sealed&&f.c.program_id==9&&f.c.hybrid.enabled);
 assert(!vxl_fused_check_program(&f.c));assert(vxl_hunet_check_access(&f.c,0x86d0df80,64,1)==-EACCES);
 input=malloc(196608);assert(input);file_read("../hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32",input,196608);
 begin.magic=VXL_HUNET_BEGIN_MAGIC;begin.version=1;begin.bytes=128;begin.run_id[0]=1;begin.sample_kind=1;begin.input_ptr=(uintptr_t)input;begin.input_bytes=196608;vxl_hunet_hash(input,196608,begin.input_sha256);
 assert(!vxl_hunet_begin(&f.c,&begin,&f.c.io));assert(!vxl_fused_check_program(&f.c));
 assert(f.c.next_sequence==1&&f.c.hybrid.gpu_jobs==0&&f.c.hybrid.logical_jobs==0);assert(!f.starts);
 vxl_hybrid_cleanup(&f.c);free(input);destroy_fixture(&f);
 puts("FUSED_NEW_BINDING_SEAL_AND_ROI_DESCRIPTOR_RESTORE_PASS_NOT_GPU");
}
