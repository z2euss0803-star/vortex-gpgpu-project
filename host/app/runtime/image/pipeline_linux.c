#include "pipeline_linux.h"
#include <errno.h>
#include <string.h>
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/generated/tables.h"

static int geometry(uint32_t w,uint32_t h){return (w==256&&h==144)||(w==1280&&h==720);}
static int rect_valid(const image_rect*r,uint32_t w,uint32_t h){
 return r&&r->w&&r->h&&r->x<w&&r->y<h&&r->w<=w-r->x&&r->h<=h-r->y;
}
static uint32_t token(unsigned slot){return IMAGE_SLOT_BASE+slot*IMAGE_SLOT_SIZE+64u;}
static unsigned cache_index(const image_params*p){return p->style==1?p->preset:p->style==2?7u:p->style==3?8u+p->preset:15u+p->preset;}
static unsigned result_slot(const image_params*p){return !p->preset?p->style+1u:p->style==1?10u+p->preset:p->style==3?16u+p->preset:22u+p->preset;}
static int params_valid(const image_params*p){
 return p&&p->style<=4&&p->preset<=6&&(!(p->style==0||p->style==2)||!p->preset)&&
  (p->style==4?(p->block==2||p->block==4||p->block==8):!p->block);
}
static int source(uint32_t p){
 if(p==IMAGE_ACTIVE)return 1;
 for(unsigned i=6;i<10;i++)if(p==token(i))return 1;
 for(unsigned i=29;i<33;i++)if(p==token(i))return 1;
 return 0;
}
static int destination(uint32_t p){for(unsigned i=2;i<33;i++)if(i!=10&&p==token(i))return 1;return 0;}
int vx_image_slot(uint32_t p,uint32_t n,uint32_t writing,uint32_t*slot,uint32_t*offset){
 if(!slot||!offset||!n||p<IMAGE_SLOT_BASE||(p&3u)||writing>1)return -EINVAL;
 uint32_t relative=p-IMAGE_SLOT_BASE,s=relative/IMAGE_SLOT_SIZE,local=relative%IMAGE_SLOT_SIZE;
 if(s>=33||local<64)return -ERANGE;
 if(s==10&&writing)return -EACCES;
 uint32_t limit=s==10?65536:IMAGE_BYTES;
 if(local-64>limit||n>limit-(local-64))return -ERANGE;
 *slot=s;*offset=local;return 0;
}
static int image_view(uint32_t value,uint32_t length,uint32_t writing,vx_image_view*v){
 int rc=vx_image_slot(value,length,writing,&v->slot,&v->offset);if(rc)return rc;
 v->length=length;v->access=writing?2:1;return 0;
}
int vx_image_lower(uint32_t op,const image_job*j,image_job*wire,vx_image_view views[5]){
 if(!wire||!views||!j)return -EINVAL;
 int rc=vx_image_job_validate(op,j);if(rc)return rc;
 if(j->param2||j->param3)return -EINVAL;
 image_job args=*j;vx_image_view refs[5];uint32_t n=j->width*j->height*4u;
 for(unsigned i=0;i<5;i++)refs[i]=(vx_image_view){10,64,64,1};
 if((rc=image_view(j->src0,n,0,&refs[0])))return rc;
 if((rc=image_view(j->dst,n,1,&refs[3])))return rc;
 if(op==IMAGE_XDOG_COLOR||op==IMAGE_NEON_COMBINE){if((rc=image_view(j->src1,n,0,&refs[1])))return rc;}
 if(op==IMAGE_NEON_COMBINE){if((rc=image_view(j->src2,n,0,&refs[2])))return rc;}
 if(j->table||op==IMAGE_PIXEL_BAYER){
  refs[4].length=65536;
  if(j->table)args.param2=j->table-IMAGE_TABLE;
  if(op==IMAGE_PIXEL_BAYER)args.param3=IMAGE_BAYER-IMAGE_TABLE;
 }
 args.src0=args.src1=args.src2=args.dst=args.table=0;
 *wire=args;memcpy(views,refs,sizeof(refs));return 0;
}
int vx_image_job_validate(uint32_t op,const image_job*j){
 if(!j||op>9||!geometry(j->width,j->height))return -EINVAL;
 image_rect r={j->rect_x,j->rect_y,j->rect_w,j->rect_h};
 if(!rect_valid(&r,j->width,j->height)||j->flags>1||!source(j->src0)||!destination(j->dst)||j->src0==j->dst)return -EINVAL;
 if(j->src1&&j->src1!=IMAGE_BAYER&&!source(j->src1))return -EINVAL;
 if(j->src2&&!source(j->src2))return -EINVAL;
 if(op==IMAGE_GAUSSIAN_H||op==IMAGE_GAUSSIAN_V){
  if(!((j->param0==3&&j->param1==7&&j->table==IMAGE_GAUSS7)||(j->param0==4&&j->param1==9&&j->table==IMAGE_GAUSS9)))return -EINVAL;
 }
 if(op==IMAGE_NEON_COMBINE&&(j->table<IMAGE_THEME||j->table>=IMAGE_THEME+224u||(j->table-IMAGE_THEME)%32u||!j->src1||!j->src2))return -EINVAL;
 if(op==IMAGE_XDOG_COLOR&&(j->table!=IMAGE_XDOG_TABLE||!j->src1))return -EINVAL;
 if(op==IMAGE_SOBEL_POSTER&&(j->table<IMAGE_PAL_Y||j->table>=IMAGE_PAL_Y+7168u||(j->table-IMAGE_PAL_Y)%1024u))return -EINVAL;
 if(op==IMAGE_PIXEL_MEAN||op==IMAGE_PIXEL_BAYER){if(j->param0!=2&&j->param0!=4&&j->param0!=8)return -EINVAL;}
 if(op==IMAGE_PIXEL_BAYER&&(j->src1!=IMAGE_BAYER||j->table<IMAGE_PAL_8||j->table>=IMAGE_PAL_8+224u||(j->table-IMAGE_PAL_8)%32u))return -EINVAL;
 uint32_t s,o,n=j->width*j->height*4u;
 if(vx_image_slot(j->src0,n,0,&s,&o)||vx_image_slot(j->dst,n,1,&s,&o))return -ERANGE;
 if(j->src1&&vx_image_slot(j->src1,j->src1==IMAGE_BAYER?64:n,0,&s,&o))return -ERANGE;
 if(j->src2&&vx_image_slot(j->src2,n,0,&s,&o))return -ERANGE;
 if(j->table&&vx_image_slot(j->table,op==IMAGE_XDOG_COLOR?16384:op==IMAGE_SOBEL_POSTER?1024:op==IMAGE_GAUSSIAN_H||op==IMAGE_GAUSSIAN_V?j->param1*4:32,0,&s,&o))return -ERANGE;
 return 0;
}
int vx_image_init(vx_image_state*s,uint32_t w,uint32_t h){
 if(!s||!geometry(w,h))return -EINVAL;
 memset(s,0,sizeof(*s));s->width=w;s->height=h;return 0;
}
int vx_image_commit(vx_image_state*s,const image_rect*r,uint32_t id,uint32_t crc){
 if(!s)return -EINVAL;
 if(s->first_error)return s->first_error;
 if(s->busy)return -EBUSY;
 if(!geometry(s->width,s->height)||!rect_valid(r,s->width,s->height)||!id||id<=s->input_id)return -EINVAL;
 if(s->epoch==UINT32_MAX)return -EOVERFLOW;
 s->rect=*r;s->input_id=id;s->input_crc=crc;++s->epoch;
 memset(s->results,0,sizeof(s->results));s->smooth_valid=s->neon_valid=s->mean_block=0;return 0;
}
static int append(vx_image_plan*p,uint32_t op,const image_job*j){
 int rc=vx_image_job_validate(op,j);if(rc)return rc;
 if(p->count>=VX_IMAGE_MAX_PASSES)return -EOVERFLOW;
 p->ops[p->count]=op;p->jobs[p->count++]=*j;return 0;
}
static int smooth(vx_image_plan*p,image_job*j){
 int rc;j->src0=IMAGE_ACTIVE;j->dst=token(6);
 if((rc=append(p,IMAGE_GRAY,j)))return rc;
 for(unsigned i=0;i<3;i++){
  j->src0=token(6+i%2);j->dst=i==2?IMAGE_SMOOTH:token(6+(i+1)%2);
  if((rc=append(p,IMAGE_BLUR3,j)))return rc;
 }
 p->next_smooth=1;return 0;
}
int vx_image_prepare(vx_image_state*s,const image_params*params,vx_image_plan*out){
 if(!s||!out||!params_valid(params)||!geometry(s->width,s->height))return -EINVAL;
 if(s->first_error)return s->first_error;
 if(s->busy)return -EBUSY;
 if(!s->input_id)return -ENODATA;
 if(s->serial==UINT32_MAX)return -EOVERFLOW;
 vx_image_plan p;memset(&p,0,sizeof(p));p.params=*params;p.width=s->width;p.height=s->height;
 p.input_id=s->input_id;p.epoch=s->epoch;p.serial=s->serial+1;p.next_smooth=s->smooth_valid;p.next_neon=s->neon_valid;p.next_mean=s->mean_block;
 p.output_token=params->style?token(result_slot(params)):IMAGE_ACTIVE;
 const vx_image_cache*c=params->style?s->results+cache_index(params):NULL;
 if(c&&c->valid&&c->epoch==s->epoch&&!memcmp(&c->params,params,sizeof(*params))){p.cache_hit=1;p.output_crc=c->crc;}
 else if(!params->style)p.output_crc=s->input_crc;
 else{
  const unsigned style=params->style,preset=params->preset;int rc;
  image_job j={.src0=IMAGE_ACTIVE,.dst=token(6),.width=s->width,.height=s->height,.rect_x=s->rect.x,.rect_y=s->rect.y,.rect_w=s->rect.w,.rect_h=s->rect.h,.flags=style==2?1u:0u};
  if((style==1||style==3)&&!s->smooth_valid){if((rc=smooth(&p,&j)))return rc;}
  if(style==1){
   j.src0=IMAGE_SMOOTH;j.dst=p.output_token;j.table=IMAGE_PAL_Y+preset*1024u;j.param0=img_themes[preset*8u+4u];
   if((rc=append(&p,IMAGE_SOBEL_POSTER,&j)))return rc;
  }else if(style==2){
   if((rc=append(&p,IMAGE_GRAY,&j)))return rc;
   for(unsigned g=0;g<2;g++){
    j.src0=token(6);j.dst=token(7);j.table=g?IMAGE_GAUSS9:IMAGE_GAUSS7;j.param0=g?4:3;j.param1=g?9:7;
    if((rc=append(&p,IMAGE_GAUSSIAN_H,&j)))return rc;
    j.src0=token(7);j.dst=token(g?9:8);if((rc=append(&p,IMAGE_GAUSSIAN_V,&j)))return rc;
   }
   j.src0=token(8);j.src1=token(9);j.dst=p.output_token;j.table=IMAGE_XDOG_TABLE;
   if((rc=append(&p,IMAGE_XDOG_COLOR,&j)))return rc;
  }else if(style==3){
   if(!s->neon_valid){
    j.src0=IMAGE_SMOOTH;j.dst=IMAGE_CORE;if((rc=append(&p,IMAGE_NEON_CORE,&j)))return rc;
    for(unsigned i=0;i<5;i++){
     j.src0=i?token(i%2?6:9):IMAGE_CORE;j.dst=i==4?IMAGE_HALO:token(i%2?9:6);
     if((rc=append(&p,IMAGE_BLUR3,&j)))return rc;
    }
    p.next_neon=1;
   }
   j.src0=IMAGE_SMOOTH;j.src1=IMAGE_CORE;j.src2=IMAGE_HALO;j.dst=p.output_token;j.table=IMAGE_THEME+preset*32u;
   if((rc=append(&p,IMAGE_NEON_COMBINE,&j)))return rc;
  }else{
   j.param0=params->block;
   if(s->mean_block!=params->block){j.src0=IMAGE_ACTIVE;j.dst=IMAGE_MEAN;if((rc=append(&p,IMAGE_PIXEL_MEAN,&j)))return rc;p.next_mean=params->block;}
   j.src0=IMAGE_MEAN;j.src1=IMAGE_BAYER;j.dst=p.output_token;j.table=IMAGE_PAL_8+preset*32u;
   if((rc=append(&p,IMAGE_PIXEL_BAYER,&j)))return rc;
  }
 }
 s->pending=p;s->serial=p.serial;s->busy=1;s->accepted=0;*out=p;return 0;
}
static int pending(const vx_image_state*s,const vx_image_plan*p){
 if(!s||!p)return -EINVAL;
 if(s->first_error)return s->first_error;
 if(!s->busy||memcmp(&s->pending,p,sizeof(*p)))return -ESTALE;
 return 0;
}
int vx_image_accept(vx_image_state*s,const vx_image_plan*p,uint32_t i,int error){
 int rc=pending(s,p);if(rc)return rc;
 if(i!=s->accepted||i>=p->count)return -ESTALE;
 if(error){s->first_error=error<0?error:-EIO;return s->first_error;}
 ++s->accepted;return 0;
}
int vx_image_finish(vx_image_state*s,const vx_image_plan*p,uint32_t crc){
 int rc=pending(s,p);if(rc)return rc;
 if(s->accepted!=p->count)return -EBUSY;
 if((p->cache_hit||!p->params.style)&&crc!=p->output_crc){s->first_error=-EBADMSG;return s->first_error;}
 s->smooth_valid=p->next_smooth;s->neon_valid=p->next_neon;s->mean_block=p->next_mean;
 if(p->params.style){vx_image_cache*c=s->results+cache_index(&p->params);c->valid=1;c->epoch=p->epoch;c->crc=crc;c->params=p->params;}
 s->busy=0;s->accepted=0;memset(&s->pending,0,sizeof(s->pending));return 0;
}
int vx_image_present_failed(vx_image_state*s,const vx_image_plan*p){
 int rc=pending(s,p);if(rc)return rc;
 if(s->accepted!=p->count)return -EBUSY;
 s->smooth_valid=p->next_smooth;s->neon_valid=p->next_neon;s->mean_block=p->next_mean;
 if(p->params.style&&!p->cache_hit)memset(s->results+cache_index(&p->params),0,sizeof(vx_image_cache));
 s->busy=0;s->accepted=0;memset(&s->pending,0,sizeof(s->pending));return 0;
}
int vx_image_cancel_current(vx_image_state*s,const vx_image_plan*p){
 int rc=pending(s,p);if(rc)return rc;
 /* Partial GPU results were never published. Keep immutable current input,
  * invalidate every derived result/intermediate rather than guess survival. */
 memset(s->results,0,sizeof(s->results));s->smooth_valid=s->neon_valid=s->mean_block=0;
 s->busy=0;s->accepted=0;memset(&s->pending,0,sizeof(s->pending));return 0;
}
