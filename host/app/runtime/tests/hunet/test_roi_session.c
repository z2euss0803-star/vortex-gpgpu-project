#include "memory_fixture.h"
int main(void){
 struct fixture *f=calloc(1,sizeof(*f));struct vxl_hunet_begin_v1 b={0};struct vxl_hunet_end_v1 end;vxl_u8 *input=malloc(196608);struct vxl_request r;unsigned i;vxl_u32 owners[1014];
 assert(f&&input);init_fixture(f);seal_fixture(f);file_read("../hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32",input,196608);
 b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.roi_index=0;b.sample_kind=1;b.run_id[0]=1;b.input_ptr=(uintptr_t)input;b.input_bytes=196608;vxl_hunet_hash(input,196608,b.input_sha256);
 f->fault=1;assert(vxl_hunet_begin(&f->c,&b,&f->c.io)==-EFAULT);assert(!f->c.active&&!f->starts);f->fault=0;
 input[0]^=1;assert(vxl_hunet_begin(&f->c,&b,&f->c.io)==-EACCES);input[0]^=1;
 f->mutate=1;assert(!vxl_hunet_begin(&f->c,&b,&f->c.io));assert(f->reads==3);assert(!memcmp(f->c.input_snapshot,f->ws+64,196608));f->mutate=0;
 r=req();r.handle=f->c.handles[1];assert(vxl_engine_call(&f->e,123,VXL_FREE,&r)==-EBUSY);r.user_ptr=(uintptr_t)input;r.length=16;assert(vxl_engine_call(&f->e,123,VXL_COPY_TO,&r)==-EACCES);
 f->c.next_sequence=1023;assert(vxl_hunet_end(&f->c,&end)==-EBUSY);f->c.next_sequence=1024;
 memset(f->ws+(VXL_HUNET_LOGITS-VXL_HUNET_WS),0,32420);for(i=0;i<1014;i++)owners[i]=i<254?1:i<508?2:i<761?3:4;
 assert(!fio(f,0x87044080,owners,sizeof(owners),1));f->c.evidence.valid_flags=VXL_HUNET_VALID_CRT;f->c.evidence.crt_guard_flags=1;
 {struct vxl_request get=req();struct vxl_hunet_end_v1 retry;struct vxl_hunet_delivery delivery;get.length=128;get.user_ptr=(uintptr_t)&end;
  f->output_fault=1;assert(vxl_engine_call(&f->e,123,VXL_HUNET_END_ROI,&get)==-EFAULT);f->output_fault=0;
  assert(f->c.active);assert(!vxl_engine_call(&f->e,123,VXL_HUNET_END_ROI,&get));retry=end;
  assert(f->c.active);b.run_id[0]=9;assert(vxl_hunet_begin(&f->c,&b,&f->c.io)==-EBUSY);b.run_id[0]=1;
  assert(!vxl_engine_call(&f->e,123,VXL_HUNET_END_ROI,&get));assert(!memcmp(&retry,&end,128));
  vxl_hunet_delivery_capture(&f->c,VXL_HUNET_END_ROI,&delivery);
  delivery.payload.end.check_flags^=1;vxl_hunet_delivery_finish(&f->c,&delivery);assert(f->c.active);
  delivery.payload.end.check_flags^=1;vxl_hunet_delivery_finish(&f->c,&delivery);assert(!f->c.active&&!f->c.end_pending);
 }
 assert(end.check_flags==31&&end.completed_jobs==1023);
 assert(vxl_hunet_begin(&f->c,&b,&f->c.io)==-EALREADY);
 {unsigned rois[3]={0,26,0};const char *paths[3]={"../hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32","../hunet_original_tcu_20261008_v1/data/board_inputs/p2_007.f32","../hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32"};for(i=0;i<3;i++){struct vxl_hunet_delivery delivery;file_read(paths[i],input,196608);b.roi_index=rois[i];b.run_id[0]=(vxl_u8)(i+2);vxl_hunet_hash(input,196608,b.input_sha256);assert(!vxl_hunet_begin(&f->c,&b,&f->c.io));f->c.next_sequence=1024;memset(f->ws+(VXL_HUNET_LOGITS-VXL_HUNET_WS),0,32420);assert(!fio(f,0x87044080,owners,sizeof(owners),1));assert(!vxl_hunet_end(&f->c,&end));vxl_hunet_delivery_capture(&f->c,VXL_HUNET_END_ROI,&delivery);vxl_hunet_delivery_finish(&f->c,&delivery);}}
 vxl_engine_close(&f->e,123);for(i=0;i<64;i++)assert(!f->e.objects[i].handle);assert(vxl_pool_reserve(&f->e.pools[0],0x2000,64)==-EBUSY);
 puts("HUNET_SNAPSHOT_FAULT_MUTATION_GRAPH_PINS_0_26_0_SESSION_ONLY_PASS");destroy_fixture(f);free(f);free(input);return 0;
}
