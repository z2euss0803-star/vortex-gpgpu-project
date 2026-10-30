#include <vxl/mosaic_runtime.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "mosaic_fixture.h"
#include "mosaic_contract_fixture.h"
#include "program_fixture.h"
_Static_assert(sizeof(struct vxl_request)==176,"public ABI unchanged");
_Static_assert(sizeof(struct vxl_mosaic_seal_v1)==128,"seal wire");
_Static_assert(sizeof(struct vxl_mosaic_frame_v1)==128,"frame wire");
struct fake {vxl_u8 *code,*weights,*data,*stack,*ps;vxl_u32 reg[64],cdma,ready,starts,acks,calls,fail_at,no_ack;vxl_u64 ns;};
static struct fake f;static struct vxl_engine e;static struct mosaic_ctx c;static struct mosaic_host_state h;
static struct mosaic_catalog_layout catalog;static struct mosaic_program program;static struct mosaic_mutable mutable[2]={{128,64},{256,1792}};
static vxl_u8 *map(vxl_u64 a,vxl_u64 n){
 struct region{vxl_u64 base,bytes;vxl_u8 *p;} regions[]={{0x80000000,0x800000,f.code},{0x80800000,0x1000000,f.weights},{0x82000000,catalog.layout.end-0x82000000,f.data},{0xfff00000,0x100000,f.stack},{0x10000000,0x100000,f.ps}};
 for(unsigned i=0;i<5;++i)if(a>=regions[i].base&&a-regions[i].base<regions[i].bytes&&n<=regions[i].bytes-(a-regions[i].base))return regions[i].p+a-regions[i].base;
 return NULL;
}
static int rw(void *x,vxl_u64 a,void *b,vxl_u64 n,int w){(void)x;assert(n&&n<=4096&&!(a&15)&&!(n&15));if(++f.calls==f.fail_at)return -EIO;vxl_u8 *p=map(a,n);if(!p)return -ERANGE;if(w)memcpy(p,b,n);else memcpy(b,p,n);return 0;}
static vxl_u32 readreg(void *x,vxl_u32 o){(void)x;assert(o<256&&!(o&3));return f.reg[o/4];}
static void writereg(void *x,vxl_u32 o,vxl_u32 v){(void)x;assert(o<256&&!(o&3));f.reg[o/4]=v;if(o==8&&v==1){++f.starts;f.reg[3]=3u<<8;}if(o==8&&v==2){++f.acks;if(!f.no_ack)f.reg[3]=0;}}
static int ready(void *x){(void)x;return f.ready;}
static vxl_u32 cdma(void *x){(void)x;return f.cdma;}
static vxl_u64 now(void *x){(void)x;return ++f.ns;}
static void put32(vxl_u8 *p,vxl_u32 n){for(unsigned i=0;i<4;++i)p[i]=(vxl_u8)(n>>(8*i));}
static vxl_u32 get32(const vxl_u8 *p){return p[0]|((vxl_u32)p[1]<<8)|((vxl_u32)p[2]<<16)|((vxl_u32)p[3]<<24);}
static void setup(const char *assets){
 memset(&f,0,sizeof(f));memset(&e,0,sizeof(e));e.profile_id=6;
 assert(!mosaic_host_init(&c,&h,&e,program_binding_sha));catalog.layout=fixture_small_layout;memcpy(catalog.graph_sha,program_graph,32);memcpy(catalog.layout_sha,program_layout,32);
 program=(struct mosaic_program){.flat=program_flat,.flat_bytes=256,.runtime_bytes=2048,.entry=0x80000000,.mutable_ranges=mutable,.mutable_count=2,.layouts=&catalog,.layout_count=1,.assets=&fixture_manifest,.assets_bytes=3382784,.crt_base=0x10002000,.crt_bytes=4096,.tls_base=0x80000200,.tbss_bytes=4,.tbss_stride=16,.tbss_align=16,.stack_stride=8192,.static_stack_bytes=1024,.stack_base=0xfff00000,.stack_bytes=0x100000};
 memcpy(&program.binding,program_binding,512);memcpy(program.binding_sha,program_binding_sha,32);memcpy(program.graph_policy_sha,program_policy,32);memcpy(program.runtime_deps_sha,program_deps,32);assert(!mosaic_program_register(&c,&program));
 struct mosaic_io io={NULL,rw,readreg,writereg,ready,cdma,now};assert(!mosaic_runtime_attach(&c,&io));assert(e.mosaic==&c&&!e.hunet&&!e.s8);
 e.opened=1;e.owner=77;e.generation=9;e.job.state=VXL_READY;h.submit_enter_ns=1;
 f.code=calloc(1,0x800000);f.weights=calloc(1,0x1000000);f.data=calloc(1,catalog.layout.end-0x82000000);f.stack=calloc(1,0x100000);f.ps=calloc(1,0x100000);assert(f.code&&f.weights&&f.data&&f.stack&&f.ps);
 memcpy(f.code,program_flat,256);FILE *file=fopen(assets,"rb");assert(file);assert(fread(f.weights,1,3382784,file)==3382784);assert(fgetc(file)==EOF);fclose(file);
 f.reg[0]=0x56580204;f.reg[1]=0x40404;f.reg[0x30/4]=15;f.reg[0x34/4]=1;f.reg[0x38/4]=0x10001;f.ready=1;f.cdma=0x1002;f.ns=10;
 const vxl_u32 gpu[5]={0x80000000,0x80800000,0x82000000,0xfff00000,0};const vxl_u64 size[5]={0x800000,0x1000000,catalog.layout.end-0x82000000,0x100000,128};
 for(unsigned i=0;i<5;++i)e.objects[i]=(struct vxl_object){.handle=(9ULL<<32)|(i+1),.length=size[i],.allocated=i==4?0:size[i],.domain=i==4?VXL_PS:VXL_PL,.purpose=i==0?VXL_CODE:i==4?VXL_ARGS:VXL_DATA,.gpu=gpu[i]};
}
static void cleanup(void){free(f.code);free(f.weights);free(f.data);free(f.stack);free(f.ps);}
static struct vxl_mosaic_seal_v1 seal(void){struct vxl_mosaic_seal_v1 p={.magic=VXL_MOSAIC_SEAL_MAGIC,.version=1,.bytes=128,.generation=9,.nonce={1}};memcpy(p.binding_sha,program_binding_sha,32);for(unsigned i=0;i<5;++i)p.handles[i]=e.objects[i].handle;return p;}
static struct vxl_mosaic_frame_v1 frame(void){struct vxl_mosaic_frame_v1 p={.magic=VXL_MOSAIC_FRAME_MAGIC,.version=1,.bytes=128,.generation=9,.input_epoch=1,.run_id={1}};memcpy(p.binding_tag,program_binding_sha,16);vxl_hunet_hash(map(catalog.layout.region[MOSAIC_FP_A].address,1152),1152,p.input_sha);h.input_written=1152;return p;}
static void load_begin(void){struct vxl_mosaic_seal_v1 s=seal();assert(!mosaic_runtime_seal(&c,&s));assert(h.loaded&&h.nonce_locked);struct vxl_mosaic_frame_v1 p=frame();assert(!mosaic_runtime_begin(&c,&p));}
static void prepare_start(void){struct mosaic_job_v1 j;assert(!mosaic_next_job(&c,1,&j));memcpy(h.args_shadow,&j,128);h.submit_enter_ns=now(NULL);struct vxl_prepared p;assert(!mosaic_prepare(&e,&p));assert(p.program_id==12&&p.code_gpu==0x80000000&&p.args_gpu==0x80100040&&p.args_word_count==32);assert(!mosaic_runtime_start(&c));assert(f.starts==1&&e.job.pinned);}
static void done(void){
 const struct mosaic_job_v1 *j=&h.evidence.args;
 f.reg[3]=2|(5u<<8);++f.reg[0x2c/4];f.reg[0x28/4]=1000;
 for(unsigned core=0;core<4;++core){vxl_u8 *p=f.ps+0x2800+core*64;memcpy(p,j->run_id,16);memcpy(p+16,j->binding_tag,16);vxl_u32 fields[8]={j->generation,j->sequence,j->op,j->layer,j->tile_first,j->tile_count,j->input_epoch,core};for(unsigned i=0;i<8;++i)put32(p+32+i*4,fields[i]);memset(f.ps+0x2008+core*256,0,4);}
}
int main(int argc,char **argv){
 assert(argc==2);setup(argv[1]);struct vxl_mosaic_seal_v1 s=seal();s.binding_sha[0]^=1;assert(mosaic_runtime_seal(&c,&s)==-ESTALE&&!h.loaded&&!f.starts);s=seal();s.handles[2]=s.handles[1];assert(mosaic_runtime_seal(&c,&s)<0&&!h.loaded);s=seal();assert(!mosaic_runtime_seal(&c,&s));assert(mosaic_runtime_seal(&c,&s)==-EBUSY);struct vxl_mosaic_frame_v1 p=frame();p.input_sha[0]^=1;assert(mosaic_runtime_begin(&c,&p)<0&&!h.frame_active);p=frame();assert(!mosaic_runtime_begin(&c,&p));prepare_start();done();assert(mosaic_runtime_poll(&e)==1);assert(f.acks==1&&!e.job.pinned&&h.evidence.observed.elapsed_raw==1000);
 vxl_u8 wire[512];assert(!mosaic_evidence_encode(&c,wire));assert(get32(wire)==0x31454d56&&get32(wire+164)==1000&&get32(wire+168)==1);for(unsigned i=448;i<512;++i)assert(wire[i]==0);assert(mosaic_reap(&c)==-EBUSY);assert(!mosaic_evidence_delivered(&c,77,e.job.id,0,512,1));assert(!mosaic_reap(&c));cleanup();
 for(unsigned fault=0;fault<7;++fault){setup(argv[1]);load_begin();prepare_start();done();switch(fault){case 0:f.no_ack=1;break;case 1:f.ps[0x2800]^=1;break;case 2:put32(f.ps+0x2008,7);break;case 3:f.data[0]^=1;break;case 4:f.code[0]^=1;break;case 5:f.reg[0x20/4]=1;break;case 6:f.reg[0x28/4]=0;break;}assert(mosaic_runtime_poll(&e)<0&&e.quarantined&&e.job.pinned);assert(f.starts==1);assert(f.acks==(fault==0?1u:0u));int first=h.first_error;assert(mosaic_runtime_poll(&e)==first&&f.starts==1);assert(!mosaic_evidence_encode(&c,wire)&&get32(wire+168)==0);printf("MOSAIC_RUNTIME_FAULT case=%u result=%d starts=%u acks=%u pinned=%u model_raw=%u raw_gpu_cycles=null\n",fault,first,f.starts,f.acks,e.job.pinned,get32(wire+164));cleanup();}
 setup(argv[1]);f.fail_at=f.calls+1;s=seal();assert(mosaic_runtime_seal(&c,&s)==-EIO&&e.quarantined&&e.job.pinned&&!f.starts);cleanup();
 setup(argv[1]);s=seal();assert(!mosaic_runtime_seal(&c,&s));put32(map(catalog.layout.region[MOSAIC_FP_A].address,4),0x7fc00000);p=frame();assert(mosaic_runtime_begin(&c,&p)==-EDOM&&!h.frame_active&&!f.starts);cleanup();
 setup(argv[1]);s=seal();assert(!mosaic_runtime_seal(&c,&s));p=frame();f.fail_at=f.calls+1;assert(mosaic_runtime_begin(&c,&p)==-EIO&&e.quarantined&&e.job.pinned&&!f.starts);cleanup();
 struct vxl_request request={.abi_major=1,.struct_size=176,.profile_id=6,.generation=9,.length=512,.user_ptr=0x1000};assert(!mosaic_uapi_validate(VXL_MOSAIC_BINDING,&request,176,0));assert(vxl_uapi_validate(VXL_MOSAIC_BINDING,&request,176,0)==-ENOTTY);request.profile_id=5;assert(mosaic_uapi_validate(VXL_MOSAIC_BINDING,&request,176,0)==-ENOTTY);
 puts("ACTUAL_MOSAIC_RUNTIME_CODE_WITH_FAKE_MMIO_CDMA_PASS_NOT_BOARD");return 0;
}
