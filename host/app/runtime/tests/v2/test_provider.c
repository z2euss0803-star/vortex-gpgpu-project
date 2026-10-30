/* Catches cross-hardware numeric-ID aliasing, private PS/bounce aliasing,
 * missing transfer IOC, CPU-pointer GPU encoding and busy TEX reprogramming. */
#include <vxl/v2/provider.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static void digest(vxl_u8 out[32],const char *hex){for(unsigned i=0;i<32;i++){unsigned n;assert(sscanf(hex+2*i,"%2x",&n)==1);out[i]=(vxl_u8)n;}}
static struct vxl_v2_boot_identity s8(void){struct vxl_v2_boot_identity b={.provider={.flags=1},.compatible="vortex,hw4-s8-demo-v1"};digest(b.provider.hardware_contract,"1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");digest(b.provider.bit,"94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");digest(b.provider.xsa,"02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");return b;}
static struct vxl_v2_boot_identity p8(void){struct vxl_v2_boot_identity b={.provider={.flags=2},.compatible="vortex,w4-graphics-p8-v1"};digest(b.provider.hardware_contract,"22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012");digest(b.provider.bit,"faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc");digest(b.provider.xsa,"08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536");return b;}
static struct vxl_v2_catalog_entry entry(struct vxl_v2_boot_identity b,unsigned ns){struct vxl_v2_catalog_entry e={.namespace_id=ns,.logical_profile=5,.logical_program=9};e.provider_flags=b.provider.flags;memcpy(e.hardware_contract,b.provider.hardware_contract,32);memcpy(e.bit,b.provider.bit,32);memcpy(e.xsa,b.provider.xsa,32);return e;}
struct hardware {vxl_u32 regs[32],dma[16],writes,completed,ack_fault;vxl_u64 ms;int locked;vxl_u8 *ps,pl[4096];};
static int locked(void *p){return ((struct hardware *)p)->locked;}
static int ready(void *p){(void)p;return 1;}
static vxl_u32 read_reg(void *p,vxl_u32 off){return ((struct hardware *)p)->regs[off/4];}
static void write_reg(void *p,vxl_u32 off,vxl_u32 value){struct hardware *h=p;h->writes++;h->regs[off/4]=value;}
static vxl_u32 dma_read(void *p,vxl_u32 off){return ((struct hardware *)p)->dma[off/4];}
static void dma_write(void *p,vxl_u32 off,vxl_u32 value){struct hardware *h=p;h->dma[off/4]=value;
 if(off==4){h->dma[1]=2;return;}
 if(off==0x28){vxl_u64 src=h->dma[6]|((vxl_u64)h->dma[7]<<32),dst=h->dma[8]|((vxl_u64)h->dma[9]<<32);vxl_u8 *a,*b;
  assert((src>=0x10000000 && src+value<=0x10100000) || (src>=0x500000000ULL && src+value<=0x500001000ULL));
  assert((dst>=0x10000000 && dst+value<=0x10100000) || (dst>=0x500000000ULL && dst+value<=0x500001000ULL));
  a=src<0x80000000?h->ps+src-0x10000000:h->pl+src-0x500000000ULL;b=dst<0x80000000?h->ps+dst-0x10000000:h->pl+dst-0x500000000ULL;
  memcpy(b,a,value);h->dma[1]=h->completed?0x1002:2;
 }}
static vxl_u64 dma_now(void *p){return ((struct hardware *)p)->ms;}
static void dma_pause(void *p){((struct hardware *)p)->ms++;}
static void barrier(void *p,int write){(void)p;(void)write;}
int main(void){struct vxl_v2_boot_identity a=s8(),b=p8();struct vxl_v2_catalog_entry e=entry(a,1);struct vxl_v2_ctx c;struct hardware io={.locked=1,.completed=1};struct vxl_v2_hw h={0};struct vxl_v2_ref data;struct vxl_v2_job j={0};struct vxl_v2_tex t={.enabled=1,.bilinear=1,.width=2,.height=2,.pitch=8};vxl_u8 input[31],output[31];struct vxl_v2_object private_stack={.domain=1,.purpose=3,.gpu=0x100ff000,.bus=0x100ff000,.offset=0xff000,.length=4096,.allocated=4096};
 assert(vxl_v2_s8_admit(&a,&e)==0);assert(vxl_v2_p8_admit(&b,&e)==-EXDEV);e=entry(b,2);assert(vxl_v2_p8_admit(&b,&e)==0);assert(vxl_v2_s8_admit(&a,&e)==-EXDEV);
 b.provider.bit[0]^=1;assert(vxl_v2_p8_admit(&b,&e)==-EXDEV);b=p8();strcpy(b.compatible,"vortex,hw4-s8-demo-v1");assert(vxl_v2_p8_admit(&b,&e)==-EXDEV);
 {struct vxl_v2_boot_identity joint=s8();joint.provider.flags=4;strcpy(joint.compatible,"vortex,hw4-s8-joint-v1");
  e=entry(joint,1);e.logical_profile=1;e.logical_program=65552;assert(vxl_v2_s8_admit(&joint,&e)==0);
  e.logical_program=65568;assert(vxl_v2_s8_admit(&joint,&e)==0);e.logical_program=65537;assert(vxl_v2_s8_admit(&joint,&e)==-EXDEV);
  e=entry(joint,2);assert(vxl_v2_s8_admit(&joint,&e)==0);assert(vxl_v2_p8_admit(&joint,&e)==-EXDEV);
  e=entry(joint,3);e.logical_profile=256;e.logical_program=512;assert(vxl_v2_s8_admit(&joint,&e)==0);e.logical_program=522;assert(vxl_v2_s8_admit(&joint,&e)==-EXDEV);
  joint.provider.bit[0]^=1;assert(vxl_v2_s8_admit(&joint,&e)==-EXDEV);
 }
 io.ps=calloc(1,0x100000);assert(io.ps);h.ctx=&c;h.identity=a;h.ps=io.ps;h.ps_dma=0x10000000;h.os=&io;h.locked=locked;h.ready=ready;h.read=read_reg;h.write=write_reg;h.memory_barrier=barrier;h.cdma=(struct vxl_cdma_io){&io,dma_read,dma_write,dma_now,dma_pause,0};io.dma[1]=2;io.regs[0]=0x56580204;io.regs[1]=0x40404;io.regs[0x34/4]=1;
 vxl_v2_init(&c,&vxl_v2_native_memory_ops,&h);assert(vxl_v2_owner_enter(&c,1,1)==0);
 assert(vxl_v2_native_memory_ops.allocate(&h,&private_stack)==0 && private_stack.storage==io.ps+0xff000);
 private_stack.offset=0xfe000;private_stack.bus=private_stack.gpu=0x100fe000;assert(vxl_v2_native_memory_ops.allocate(&h,&private_stack)==-ERANGE);
 assert(vxl_v2_buffer_create(&c,1,1,2,3,3,64,&data)==0);for(unsigned i=0;i<31;i++)input[i]=(vxl_u8)(i+1);data.offset=1;data.length=31;
 assert(vxl_v2_buffer_copy(&c,1,&data,input,1)==0);assert(vxl_v2_buffer_copy(&c,1,&data,output,0)==0 && !memcmp(input,output,31));assert(io.pl[0]==0 && io.pl[32]==0);assert(h.dma_completed && h.cdma.last_status==0x1002);
 assert(vxl_v2_hw_idle(&h)==0);io.dma[1]=2;assert(vxl_v2_hw_idle(&h)==-EIO);h.dma_completed=0;assert(vxl_v2_hw_idle(&h)==0);
 data.offset=0;data.length=64;data.access=1;assert(vxl_v2_buffer_pin(&c,1,&data,2)==0);j.num_refs=1;j.refs[0]=data;
 io.regs[3]=0x100;assert(vxl_v2_prepare_tex(&h,&j,&t)==-EBUSY && io.writes==0);io.regs[3]=0;
 assert(vxl_v2_prepare_tex(&h,&j,&t)==0);assert(io.regs[0x40/4]==0x80000000 && io.regs[0x50/4]==9 && io.regs[0x44/4]==2);
 assert(vxl_v2_buffer_unpin(&c,1,&data,2)==0);data.access=3;assert(vxl_v2_buffer_release(&c,1,&data)==0);
 {struct vxl_v2_ref ps[4],extra;vxl_u64 sizes[4]={0x2000,0x2d000,0xcd000,0xfc0};
  for(unsigned i=0;i<4;i++)assert(vxl_v2_buffer_create(&c,1,1,1,3,3,sizes[i],&ps[i])==0);
  /* Remaining PS is the new bounce region and private stack, both reserved. */
  assert(vxl_v2_buffer_create(&c,1,1,1,3,3,64,&extra)==-ENOMEM && c.live==4);
  for(unsigned i=0;i<4;i++)assert(vxl_v2_buffer_release(&c,1,&ps[i])==0);
 }
 /* Requiring IOC on an actual transfer differs from accepting cold idle. */
 io.completed=0;io.ms=0;assert(vxl_v2_buffer_create(&c,1,1,2,3,3,64,&data)==0);data.length=16;assert(vxl_v2_buffer_copy(&c,1,&data,input,1)==-ETIMEDOUT && c.quarantined);
 free(io.ps);puts("V2_PROVIDER_IDENTITY_NATIVE_PS_RMW_CDMA_IOC_TEX_IDLE_PASS_NOT_BOARD");return 0;}
