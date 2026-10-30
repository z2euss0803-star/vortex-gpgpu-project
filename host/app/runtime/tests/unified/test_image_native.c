/* Actual portable C catalog/load/relocation/pin/ACK. Hardware is a host fixture;
 * it produces no pixels and no real GPU cycles. */
#include "image/pipeline_linux.h"
#include "../v2/jobs_fixture.h"
#include <vxl/hunet.h>
_Alignas(4) static unsigned char expected_args[64];
static int image_prepare(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,const void*a,vxl_u32 n){(void)p;(void)program;assert(n==64&&j->num_refs==5&&!memcmp(a,expected_args,64));return 0;}
static const struct vxl_v2_execution_ops image_ops={image_prepare,start,ack,now};
int main(int argc,char**argv){
 assert(argc==3);unsigned char catalog[65537];size_t bytes=read_fixture(argv[1],catalog,sizeof(catalog));struct vxl_v2_catalog_entry e;struct vxl_v2_provider provider;struct fixture f;
 memset(&f,0,sizeof(f));memset(&provider,0,sizeof(provider));memcpy(&e,catalog+64,512);provider.flags=1;memcpy(provider.hardware_contract,e.hardware_contract,32);memcpy(provider.bit,e.bit,32);memcpy(provider.xsa,e.xsa,32);
 assert(!vxl_v2_catalog_open(&f.cat,catalog,(vxl_u32)bytes,&provider)&&f.cat.count==10);
 vxl_v2_init(&f.c,&program_ops,&f.memory);assert(!vxl_v2_table_bind(&f.c,&f.table,&f.cat));assert(!vxl_v2_owner_enter(&f.c,1,1));f.q=calloc(1,sizeof(*f.q));assert(f.q);f.hw.now=100;f.hw.jobs=7;assert(!vxl_v2_queue_bind(&f.c,f.q,&image_ops,&f.hw));
 struct vxl_v2_ref arena,args;assert(!vxl_v2_buffer_create(&f.c,1,1,VXL_PL,VXL_V2_DATA,3,VX_IMAGE_RESERVED_BYTES,&arena));assert(!vxl_v2_buffer_create(&f.c,1,1,VXL_PL,VXL_V2_ARGS,3,64,&args));
 uint32_t marker=0x711495ea,readback=0;struct vxl_v2_ref tag=arena;tag.length=4;assert(!vxl_v2_buffer_copy(&f.c,1,&tag,&marker,1));
 vx_image_state state;assert(!vx_image_init(&state,256,144));image_rect rect={3,4,247,131};assert(!vx_image_commit(&state,&rect,1,0));image_job original[10];unsigned seen=0;
 for(unsigned style=1;style<=4;style++){
  image_params params={style,style==1?1:0,style==4?4:0};vx_image_plan p;assert(!vx_image_prepare(&state,&params,&p));
  for(unsigned i=0;i<p.count;i++){original[p.ops[i]]=p.jobs[i];seen|=1u<<p.ops[i];assert(!vx_image_accept(&state,&p,i,0));}assert(!vx_image_finish(&state,&p,style));
 }
 assert(seen==1023);
 for(unsigned op=0;op<10;op++){
  image_job wire;vx_image_view views[5];int lowered=vx_image_lower(op,&original[op],&wire,views);
  if(lowered){fprintf(stderr,"U14_IMAGE_NATIVE_ASSERT checked lowering returns %d op=%u\n",lowered,op);return 84;}
  memcpy(&e,catalog+64+512*op,512);assert(e.namespace_id==3&&e.logical_profile==256&&e.logical_program==512+op);
  char path[1024],hex[65];for(unsigned k=0;k<32;k++)sprintf(hex+2*k,"%02x",e.entry_id[k]);assert(snprintf(path,sizeof(path),"%s/%s.bin",argv[2],hex)>0);unsigned char code[2049];assert(read_fixture(path,code,sizeof(code))==2048);
  struct vxl_v2_program_ref pr;code[0]^=1;assert(vxl_v2_program_load(&f.c,1,&e,code,NULL,&pr)==-EBADMSG&&!f.c.quarantined&&f.c.live==2);code[0]^=1;assert(!vxl_v2_program_load(&f.c,1,&e,code,NULL,&pr));
  assert(!vxl_v2_buffer_copy(&f.c,1,&args,&wire,1));struct vxl_v2_job j;memset(&j,0,sizeof(j));j.major=2;j.size=512;j.cookie=op+1;j.program_handle=pr.handle;j.program_generation=pr.generation;j.device_generation=1;j.execution_budget_ns=1000000;j.args_handle=args.handle;j.args_generation=args.object_generation;j.args_bytes=64;j.num_refs=5;
  for(unsigned k=0;k<5;k++){j.refs[k]=arena;j.refs[k].offset=(vxl_u64)views[k].slot*IMAGE_SLOT_SIZE+views[k].offset;j.refs[k].length=views[k].length;j.refs[k].access=views[k].access;assert(!(j.refs[k].offset&63)&&j.refs[k].length&&j.refs[k].offset+j.refs[k].length<=arena.length);}
  struct vxl_v2_program*program=NULL;for(unsigned k=0;k<16;k++)if(f.table.programs[k].ref.handle==pr.handle)program=&f.table.programs[k];assert(program);
  assert(!vxl_v2_build_args(&f.c,program,&j,expected_args,sizeof(expected_args)));
  /* Individually valid scalar bounds cannot authorize an overflowing rect.
   * The actual generic C boundary must reject it before any hardware start. */
  image_job invalid=wire;invalid.rect_x=wire.width-1;invalid.rect_w=wire.width;
  assert(!vxl_v2_buffer_copy(&f.c,1,&args,&invalid,1));
  int rejected=vxl_v2_build_args(&f.c,program,&j,expected_args,64);
  if(rejected!=-ERANGE){fprintf(stderr,"U14_SCHEMA_CROSS_FIELD_RECT_MUST_REJECT_BEFORE_START rc=%d op=%u\n",rejected,op);return 85;}
  assert(f.hw.starts==op);
  for(unsigned mutation=0;mutation<8;mutation++){
   invalid=wire;struct vxl_v2_job changed_job=j;
   if(mutation==0){invalid.rect_y=wire.height-1;invalid.rect_h=wire.height;}
   if(mutation==1){invalid.width=257;changed_job.refs[0].length=changed_job.refs[3].length=IMAGE_BYTES;}
   if(mutation==2){invalid.height=720;changed_job.refs[0].length=changed_job.refs[3].length=IMAGE_BYTES;}
   if(mutation==3){changed_job.refs[3]=changed_job.refs[0];changed_job.refs[3].access=2;}
   if(mutation==4){invalid.param3=op==9?7425:1;}
   if(mutation==5){invalid.param2++;}
   if(mutation==6){if(op==3||op==4)invalid.param1=8;else if(op==8||op==9)invalid.param0=3;else invalid.rect_w=0;}
   if(mutation==7){if(op==3||op==4)invalid.param2=invalid.param0==3?7552:7488;else invalid.rect_h=0;}
   assert(!vxl_v2_buffer_copy(&f.c,1,&args,&invalid,1));assert(vxl_v2_build_args(&f.c,program,&changed_job,expected_args,64)<0&&f.hw.starts==op);
  }
  {unsigned char checked[2048];assert(e.schema_bytes<=sizeof(checked));memcpy(checked,catalog+e.schema_offset,e.schema_bytes);struct vxl_v2_schema_header*h=(void*)checked;assert(!vxl_v2_schema_check(checked,e.schema_bytes,64));h->version=1;assert(vxl_v2_schema_check(checked,e.schema_bytes,64)<0);h->version=2;
   struct vxl_v2_schema_predicate*p=(void*)(checked+64+16*64);assert(p->kind==VXL_V2_PREDICATE32);p->operation=99;assert(vxl_v2_schema_check(checked,e.schema_bytes,64)<0);p->operation=VXL_V2_PAIR_SET;p->reserved[2]=1;assert(vxl_v2_schema_check(checked,e.schema_bytes,64)<0);p->reserved[2]=0;p->arg_offset=0;assert(vxl_v2_schema_check(checked,e.schema_bytes,64)<0);
  }
  assert(!vxl_v2_buffer_copy(&f.c,1,&args,&wire,1));assert(!vxl_v2_build_args(&f.c,program,&j,expected_args,64));
  uint32_t*values=(uint32_t*)(void*)expected_args;assert(values[0]&&values[3]&&values[4]==256&&values[5]==144);if(op==9)assert(values[1]==values[10]+(7424u-(original[op].table-IMAGE_TABLE)));
  struct vxl_v2_job stale=j;stale.refs[0].object_generation++;assert(vxl_v2_build_args(&f.c,program,&stale,expected_args,64)==-ESTALE);assert(!vxl_v2_build_args(&f.c,program,&j,expected_args,64));
  vxl_u64 seq=0;assert(!vxl_v2_accept(&f.c,1,&j,&seq));assert(!vxl_v2_pump(&f.c)&&f.c.active==1);assert(vxl_v2_program_release(&f.c,1,&pr)==-EBUSY&&vxl_v2_buffer_release(&f.c,1,&arena)==-EBUSY);
  /* Core requires a valid bounded counter. This37 is an injected HOST fixture value, never measured GPU cycles. */
  struct vxl_v2_snapshot s=snapshot(&f,seq);assert(!vxl_v2_poll(&f.c,&s));struct vxl_v2_completion completion;assert(!vxl_v2_read_completion(&f.c,1,seq,j.cookie,&completion)&&completion.state==VXL_V2_OK&&!completion.result&&completion.cycles_valid&&completion.elapsed_raw==37);
  assert(vxl_v2_program_release(&f.c,1,&pr)==-EBUSY);assert(!vxl_v2_ack_reap(&f.c,1,seq,j.cookie));assert(!vxl_v2_program_release(&f.c,1,&pr)&&f.c.live==2);assert(!vxl_v2_buffer_copy(&f.c,1,&tag,&readback,0)&&readback==marker);
 }
 assert(f.hw.starts==10&&f.hw.acks==10&&!f.c.active&&!f.q->reserved);assert(!vxl_v2_buffer_release(&f.c,1,&args));assert(!vxl_v2_buffer_release(&f.c,1,&arena));assert(!f.c.live);teardown_host_storage(&f);
 puts("U14_IMAGE10_C_LOAD_RELOCATE_BUSY_ACK_SWITCH_DATA_RETAINED_PASS_HOST_NOT_GPU injected_fixture_counter=37 gpu_raw_cycles=null");
}
