#include <string.h>
#include "image_gpu.h"
#include "host_platform.h"
#include "gpgpu_hw.h"
#include "generated/image_kernels.h"
static uint32_t rd(uint32_t off){return host_mmio_read(GPU_BASE+off);}
static void wr(uint32_t off,uint32_t v){host_mmio_write(GPU_BASE+off,v);host_barrier();}
static int source(uint32_t p){if(p==IMAGE_ACTIVE)return 1;for(unsigned i=6;i<10;i++)if(p==IMAGE_SLOT_BASE+i*IMAGE_SLOT_SIZE+64u)return 1;for(unsigned i=29;i<33;i++)if(p==IMAGE_SLOT_BASE+i*IMAGE_SLOT_SIZE+64u)return 1;return 0;}
static int destination(uint32_t p){for(unsigned i=2;i<IMAGE_SLOT_COUNT;i++)if(i!=10&&p==IMAGE_SLOT_BASE+i*IMAGE_SLOT_SIZE+64u)return 1;return 0;}
static int valid(enum image_op op,const image_job *j){
 if(!j||op<0||op>IMAGE_PIXEL_BAYER||!((j->width==1280&&j->height==720)||(j->width==256&&j->height==144)))return 0;
 if(!j->rect_w||!j->rect_h||j->rect_x>=j->width||j->rect_y>=j->height||j->rect_w>j->width-j->rect_x||j->rect_h>j->height-j->rect_y||j->flags>1u)return 0;
 if(!source(j->src0)||!destination(j->dst)||j->src0==j->dst)return 0;
 if(j->src1&&j->src1!=IMAGE_BAYER&&!source(j->src1))return 0;if(j->src2&&!source(j->src2))return 0;
 if(op==IMAGE_GAUSSIAN_H||op==IMAGE_GAUSSIAN_V){if(!((j->param0==3&&j->param1==7&&j->table==IMAGE_GAUSS7)||(j->param0==4&&j->param1==9&&j->table==IMAGE_GAUSS9)))return 0;}
 if(op==IMAGE_NEON_COMBINE&&(j->table<IMAGE_THEME||j->table>=IMAGE_THEME+224u||(j->table-IMAGE_THEME)%32u))return 0;
 if(op==IMAGE_XDOG_COLOR&&j->table!=IMAGE_XDOG_TABLE)return 0;
 if(op==IMAGE_SOBEL_POSTER&&(j->table<IMAGE_PAL_Y||j->table>=IMAGE_PAL_Y+7168u||(j->table-IMAGE_PAL_Y)%1024u))return 0;
 if(op==IMAGE_PIXEL_MEAN||op==IMAGE_PIXEL_BAYER){if(j->param0!=2&&j->param0!=4&&j->param0!=8)return 0;}
 if(op==IMAGE_PIXEL_BAYER&&(j->src1!=IMAGE_BAYER||j->table<IMAGE_PAL_8||j->table>=IMAGE_PAL_8+224u||(j->table-IMAGE_PAL_8)%32u))return 0;
 return 1;
}
int image_gpu_init(void){
 if(rd(GPU_ID)!=0x56580204u||rd(GPU_CONFIG)!=0x00040404u||rd(GPU_CAPABILITY)!=0x00010001u||!(rd(GPU_MEMORY_READY)&1u)||rd(GPU_STATUS)||rd(GPU_ERRORS)||rd(GPU_PENDING_WRITES)||rd(GPU_PENDING_READS))return -6;
 return 0;
}
int image_gpu_run(enum image_op op,const image_job *j,image_gpu_stats *s){
 if(!s||!valid(op,j))return -4;if(rd(GPU_STATUS))return -6;
 uint32_t words=img_kernel_words[op],rounded=(words+15u)&~15u;if(rounded*4u>65536u)return -4;
 uint32_t args[16];memcpy(args,j,64);s->jobs_before=rd(GPU_JOBS);
 for(uint32_t i=0;i<rounded;i++)host_ps_write(PS_CODE+4u*i,i<words?img_kernels[op][i]:0x00100073u);
 for(unsigned i=0;i<16;i++)host_ps_write(PS_ARGS+4u*i,args[i]);host_flush(PS_CODE,rounded*4u);host_flush(PS_ARGS,64);host_barrier();
 /* New image passes have a bounded watchdog, not old fixed polling iterations. */
 wr(GPU_PC,(uint32_t)PS_CODE);wr(GPU_ARG,(uint32_t)PS_ARGS);wr(GPU_WATCHDOG,1000000000u);
 for(unsigned off=0x40;off<=0x54;off+=4)wr(off,0);
 uint64_t begin=image_ticks(),limit=15u*image_timer_hz();wr(GPU_COMMAND,1);uint32_t status;
 for(;;){status=rd(GPU_STATUS);if((status&GPU_STATUS_ERROR)||((status>>GPU_STATUS_STATE_SHIFT)&7u)==GPU_STATE_FAULT)break;if(status&GPU_STATUS_DONE)break;if(image_ticks()-begin>=limit){status|=GPU_STATUS_ERROR;break;}host_service_poll();}
 s->jobs_after=rd(GPU_JOBS);s->cycles=rd(GPU_ELAPSED);s->errors=rd(GPU_ERRORS);s->pending_rd=rd(GPU_PENDING_READS);s->pending_wr=rd(GPU_PENDING_WRITES);
 host_log("IMAGE_GPU_PASS op=%u jobs_before=%u jobs_after=%u cycles=%u status=%08x errors=%08x pending_rd=%u pending_wr=%u\r\n",(unsigned)op,s->jobs_before,s->jobs_after,s->cycles,status,s->errors,s->pending_rd,s->pending_wr);
 if(!(status&GPU_STATUS_DONE)||(status&GPU_STATUS_ERROR)||s->errors||s->pending_rd||s->pending_wr||s->jobs_after!=s->jobs_before+1u)return -6;
 host_barrier();wr(GPU_COMMAND,2);return rd(GPU_STATUS)?-6:0;
}
