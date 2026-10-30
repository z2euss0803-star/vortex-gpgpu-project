/* TEST_ONLY boundary observations; no GPU/RTL is executed. */
#include <vxl/mock.h>
#include <vxl/s8.h>
#include <assert.h>
#include <stdlib.h>
struct state_fixture {struct vxl_mock m;struct vxl_s8_ctx c;vxl_u64 output;};
static struct vxl_request sreq(void) {struct vxl_request r={.abi_major=1,.struct_size=176};return r;}
static void state_init(struct state_fixture *f) {
 vxl_u8 tag[16]={0x55};vxl_mock_init(&f->m);
 assert(!vxl_engine_init_profile(&f->m.engine,f->m.engine.ops,&f->m,1,5));
 assert(!vxl_s8_context_init(&f->c,&f->m.engine,tag));assert(!vxl_engine_open(&f->m.engine,1));
 struct vxl_request r=sreq();r.domain=VXL_PL;r.purpose=VXL_DATA;r.address=0x81400000;r.length=4096;
 assert(!vxl_engine_call(&f->m.engine,1,VXL_RESERVE,&r));f->output=r.handle;
 struct vxl_s8_job_v1 j={.magic=0x314a3853,.version=1,.bytes=128,.case_id=101,.k=1,.core_mask=1,
 .a_base=0x81000040,.b_base=0x81200040,.input_slot_stride=0x20000,.output_base=0x81400000,.output_stride=4096,
 .identity_base=0x81410000,.control_base=0x81411000,.spin_limit=65536,.generation=f->m.engine.generation,.sequence=1,.run_id={1},.binding_tag={0x55}};
 j.args_crc32=vxl_s8_job_crc(&j);assert(!vxl_s8_accept_job(&f->c,&j));
 f->m.engine.job.id=1;f->m.engine.job.jobs_before=0;struct vxl_event start={.kind=VXL_START,.id=1};
 assert(!vxl_job_step(&f->m.engine.job,&start));
 f->c.hw=(struct vxl_s8_hw_snapshot){.job_id=1,.generation=f->m.engine.generation,.sequence=1,.elapsed_raw=123456,.elapsed_valid=1,.pre_ack=1,
 .watchdog=4000000000u,.cdma_status=0x1002,.exit_valid=15,.guard_valid=1,.immutable_valid=1,.input_valid=1,.ack_valid=1};
 f->c.hw.event=(struct vxl_event){.kind=VXL_DONE,.id=1,.status=0x502,.jobs=1,.ready=1};
 f->c.hw.elapsed_address=0xa0000028ULL;f->c.hw.ack_idle=1;f->c.hw.ack_ready=1;
 f->c.hw.identity_valid=1;f->c.hw.metadata_valid=1;f->c.hw.metadata_count=1;
 for(unsigned index=0;index<3;++index)for(unsigned j0=0;j0<64;++j0)f->c.hw.crt_guards[index*64+j0]=vxl_s8_guard_byte(9,0,index,j0);
}
static struct vxl_event done_event(void) {struct vxl_event ev={.kind=VXL_DONE,.id=1,.status=0x502,.jobs=1,.ready=1,.idle=1};return ev;}
static void state_complete(struct state_fixture *f) {
 struct vxl_event ev=done_event();assert(!vxl_s8_complete(&f->m.engine,&ev));assert(!vxl_job_step(&f->m.engine.job,&ev));
}
static void state_destroy(struct state_fixture *f) {
 /* Synthetic memory only: never a recovery/free mechanism for real hardware. */
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)free(f->m.engine.objects[i].storage);
}
