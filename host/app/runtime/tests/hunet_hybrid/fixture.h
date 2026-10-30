#ifndef HYBRID_TEST_FIXTURE_H
#define HYBRID_TEST_FIXTURE_H
#include <vxl/hunet_hybrid.h>
#include "../hunet/memory_fixture.h"
/* Registers and byte transport are injected, never model arithmetic. */
struct hfixture {struct fixture f;struct vxl_hybrid_release release;struct vxl_ops ops;unsigned read_blocks,write_blocks,max_block,fail_write,fail_read,corrupt_write,corrupt_guard;int input_fault;vxl_u64 write_tick_ns;};
static int hio(void *p,vxl_u64 a,void *b,vxl_u64 n,int w){
 struct hfixture *h=p;assert(n<=4096&&!(a&15)&&!(n&15));if(n>h->max_block)h->max_block=(unsigned)n;
 if(w){h->write_blocks++;if(h->fail_write&&h->write_blocks==h->fail_write)return -EIO;}
 else{h->read_blocks++;if(h->fail_read&&h->read_blocks==h->fail_read)return -EIO;}
 int rc=fio(&h->f,a,b,n,w);
 if(!rc&&w){
  if(h->corrupt_write&&h->write_blocks==h->corrupt_write)*map(&h->f,a,1)^=1;
  if(h->corrupt_guard&&h->write_blocks==h->corrupt_guard)*map(&h->f,a+n,1)^=1;
  h->f.clock_ns+=h->write_tick_ns;
 }
 return rc;
}
static int hread_user(void *p,void *dst,vxl_u64 src,vxl_u64 n){struct hfixture *h=p;if(h->input_fault)return -EFAULT;return fread_user(&h->f,dst,src,n);}
static void hinit(struct hfixture *h){
 memset(h,0,sizeof(*h));init_fixture(&h->f);h->ops=fops;h->ops.read_user=hread_user;h->f.e.ops=&h->ops;h->f.c.io.rw=hio;
 struct vxl_hybrid_release *r=&h->release;r->binding.v1=h->f.r.binding;r->binding.v1.version=2;r->binding.v1.bytes=384;r->binding.v1.program_id=8;r->binding.v1.features=7;
 memset(r->binding.cpu_binary_sha256,1,32);memset(r->binding.cpu_contract_sha256,2,32);memset(r->binding.cpu_stages_sha256,3,32);vxl_hunet_hash(&r->binding,384,r->binding_sha);
 assert(!vxl_hybrid_init(&h->f.c,r));struct vxl_hunet_seal_v1 s={.magic=VXL_HYBRID_SEAL_MAGIC,.version=1,.bytes=128};
 s.code=reserve_fixture(&h->f,VXL_CODE,VXL_HUNET_CODE,8388608);s.weights=reserve_fixture(&h->f,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864);s.workspace=reserve_fixture(&h->f,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES);s.args=reserve_fixture(&h->f,VXL_ARGS,0,128);memcpy(s.binding_sha256,r->binding_sha,32);assert(!vxl_hybrid_seal(&h->f.c,&s));
 assert(!vxl_hunet_guards(&h->f.c,1));h->f.e.snapshot.ready=h->f.e.snapshot.idle=1;h->f.c.active=1;h->f.c.next_sequence=296;h->f.c.run_id[0]=0x12;vxl_hybrid_start_roi(&h->f.c);h->read_blocks=h->write_blocks=0;
}
static struct vxl_hybrid_begin_v1 hbegin_request(struct hfixture *h,void *out){
 const vxl_u32 *j=vxl_hunet_catalog_job(h->f.c.next_sequence);
 struct vxl_hybrid_begin_v1 b={.magic=VXL_HYBRID_BEGIN_MAGIC,.version=1,.bytes=128,.generation=h->f.e.generation,.sequence=j[4],.stage=j[30],.dtype=1,.input_ptr=(uintptr_t)out,.input_bytes=(vxl_u64)j[7]*4,.output_bytes=(vxl_u64)j[7]*4};memcpy(b.run_id,h->f.c.run_id,16);return b;
}
static void hfree(struct hfixture *h){vxl_hybrid_cleanup(&h->f.c);destroy_fixture(&h->f);}
#endif
