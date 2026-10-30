/* TEST_ONLY static release; this fixture is never a deployable binding. */
#include <vxl/s8.h>
#include <vxl/mock.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct vxl_mock mock;
static vxl_u8 *code,*args,crt[4096];
static int fail_write;
static int io(void *unused,vxl_u64 address,void *data,vxl_u64 n,int write) {
 vxl_u8 *p=0;(void)unused;
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i) {
  struct vxl_object *o=&mock.engine.objects[i];
  if(o->handle && address>=o->gpu && address+n<=(vxl_u64)o->gpu+o->allocated)p=(vxl_u8 *)o->storage+address-o->gpu;
 }
 if(address>=0x10002000ULL && address+n<=0x10003000ULL)p=crt+address-0x10002000ULL;
 if(!p)return -ERANGE;
 if(write && fail_write)return -EIO;
 if(write)memcpy(p,data,n);else memcpy(data,p,n);return 0;
}
int main(void) {
 struct vxl_s8_ctx c;struct vxl_prepared prepared;
 vxl_u8 flat[256];for(unsigned i=0;i<sizeof(flat);++i)flat[i]=(vxl_u8)i;
 struct vxl_s8_mutable mutable[2]={{128,64},{256,8960}};
 struct vxl_s8_program p={.flat=flat,.flat_bytes=256,.entry=0x80000000,.mutable_ranges=mutable,.mutable_count=2,
 .layout={.regions={{0x80000000ULL,0x100000},{0x80100000ULL,0x1000},{0x81000000ULL,0x200000},{0x81200000ULL,0x200000},{0x81400000ULL,0x10000},{0x81410000ULL,4096},{0x81411000ULL,4096},{0xfff70000ULL,0x80040}},.runtime_bytes=9216,.tls_base=0x80002000,.tbss_bytes=4,.tbss_stride=16,.tbss_align=16,.static_stack_bytes=4096,.objects_count=53},
 .binding={.magic=0x31423853,.version=1,.bytes=512,.profile=5,.program=11,.abi_major=1,.features=1}};
 for(unsigned i=0;i<14;++i)memset(p.binding.digests[i],i+1,32);
 vxl_hunet_hash(flat,sizeof(flat),p.flat_sha);memcpy(p.binding.digests[1],p.flat_sha,32);
 vxl_hunet_hash(&p.binding,sizeof(p.binding),p.binding_sha);
 vxl_mock_init(&mock);assert(!vxl_engine_init_profile(&mock.engine,mock.engine.ops,&mock,1,5));
 assert(!vxl_s8_context_init(&c,&mock.engine,p.binding_sha));c.io=(struct vxl_s8_io){.context=0,.rw=io};
 assert(!vxl_engine_open(&mock.engine,1));assert(vxl_s8_prepare(&mock.engine,&prepared)==-ENODEV);
 for(unsigned region=0;region<8;++region) {
  unsigned slots=region>=2 && region<=4?16:1;
  for(unsigned slot=0;slot<slots;++slot) {
   struct vxl_request r={.abi_major=1,.struct_size=176,.domain=VXL_PL,.purpose=region==0?VXL_CODE:(region==1?VXL_ARGS:VXL_DATA),.address=p.layout.regions[region].begin+slot*(p.layout.regions[region].size/slots),.length=p.layout.regions[region].size/slots};
   assert(!vxl_engine_call(&mock.engine,1,VXL_RESERVE,&r));
  }
 }
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i) {
  if(mock.engine.objects[i].gpu==0x80000000)code=mock.engine.objects[i].storage;
  if(mock.engine.objects[i].gpu==0x80100000)args=mock.engine.objects[i].storage;
 }
 assert(code && args && mock.allocations==53);
 struct vxl_s8_input_digest input={.case_id=110,.slot=0};vxl_u8 zero[16]={0};
 vxl_hunet_hash(zero,16,input.a_sha);memcpy(input.b_sha,input.a_sha,32);p.inputs=&input;p.input_count=1;
 assert(!vxl_s8_program_register(&c,&p));
 memcpy(code,flat,sizeof(flat));
 code[128]^=0xff;memset(code+256,0x55,8960);
 assert(!vxl_s8_restore(&mock.engine,&p.layout));
 assert(!memcmp(code,flat,256));for(unsigned i=256;i<9216;++i)assert(!code[i]);
 assert(c.restore_bytes==9024 && c.restore_ops>0);
 code[128]^=0xff;memset(code+8192,0xee,1024);
 assert(!vxl_s8_restore(&mock.engine,&p.layout));assert(!memcmp(code,flat,256));
 for(unsigned i=8192;i<9216;++i)assert(!code[i]);
 code[7]^=1;assert(vxl_s8_immutable_check(&c)==-EACCES);code[7]^=1;
 struct vxl_s8_program bad=p;bad.entry=0x10000000;assert(vxl_s8_program_register(&c,&bad)<0);
 bad=p;bad.layout.runtime_bytes=0xf0001;assert(vxl_s8_program_register(&c,&bad)<0);
 bad=p;bad.flat_sha[1]^=1;assert(vxl_s8_program_register(&c,&bad)<0);
 struct vxl_s8_job_v1 j={.magic=0x314a3853,.version=1,.bytes=128,.case_id=110,.k=1,.core_mask=1,
 .a_base=0x81000040,.b_base=0x81200040,.input_slot_stride=0x20000,.output_base=0x81400000,.output_stride=0x1000,
 .identity_base=0x81410000,.control_base=0x81411000,.spin_limit=65536,.generation=mock.engine.generation,.sequence=1,.run_id={1}};
 memcpy(j.binding_tag,p.binding_sha,16);j.args_crc32=vxl_s8_job_crc(&j);memcpy(args+64,&j,128);
 assert(!vxl_s8_prepare(&mock.engine,&prepared));assert(c.job_args.sequence==1 && prepared.program_id==11 && prepared.args_gpu==0x80100040);
 code[128]^=1;memset(code+8192,0x55,1024);j.sequence=2;j.args_crc32=vxl_s8_job_crc(&j);memcpy(args+64,&j,128);
 assert(!vxl_s8_prepare(&mock.engine,&prepared));assert(c.job_args.sequence==2 && c.restore_bytes==9024);
 assert(!memcmp(code,flat,256));for(unsigned i=8192;i<9216;++i)assert(!code[i]);
 assert(!vxl_s8_guards(&c,&c.job_args,0));
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)if(mock.engine.objects[i].gpu==0x81400000) {
  vxl_u8 *output=mock.engine.objects[i].storage;output[2240]^=1;
  assert(vxl_s8_guards(&c,&c.job_args,0)==-EIO);output[2240]^=1;
 }
 crt[5]^=1;assert(vxl_s8_guards(&c,&c.job_args,0)==-EIO);crt[5]^=1;
 fail_write=1;assert(vxl_s8_restore(&mock.engine,&p.layout)==-EIO);
 assert(mock.starts==0 && mock.engine.job.pinned && mock.engine.quarantined);
 /* Test-only dismantling of synthetic memory, never hardware recovery. */
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)free(mock.engine.objects[i].storage);
 puts("S8_PROGRAM_RESTORE_PASS TEST_ONLY two_prepares=2 no_gpu_execution=1");return 0;
}
