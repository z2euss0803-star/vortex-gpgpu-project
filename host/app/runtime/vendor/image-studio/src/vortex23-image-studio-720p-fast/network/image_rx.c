#include <string.h>
#include "image_rx.h"
static uint8_t *stage,bitmap[((IMAGE_BYTES+IMAGE_CHUNK_BYTES-1u)/IMAGE_CHUNK_BYTES+7u)/8u],metrics[64];static uint32_t capacity,session,received,state,reason,peer_ip,active_id,active_crc,last_tx;static uint16_t peer_port;
static int pinned,new_image,has_metrics;static image_header current;static image_rect rect;static uint64_t started,last;
static char bundle[64];static int bundle_ready;
void image_rx_init(uint8_t *s,uint32_t n,uint32_t boot){stage=s;capacity=n;session=boot?boot:1;received=state=reason=active_id=active_crc=last_tx=0;pinned=new_image=has_metrics=bundle_ready=0;memset(&current,0,sizeof(current));current.width=1280;current.height=720;memset(bitmap,0,sizeof(bitmap));}
void image_rx_set_active(uint32_t id,uint32_t crc){active_id=id;active_crc=crc;}
void image_rx_set_bundle(const char *b){if(!b||strlen(b)!=64)return;for(unsigned i=0;i<64;i++)if(!((b[i]>='0'&&b[i]<='9')||(b[i]>='a'&&b[i]<='f')))return;memcpy(bundle,b,64);bundle_ready=1;}
static int identity(const image_header *h){return h->session==session&&h->tx==current.tx&&h->input_id==current.input_id&&h->style==current.style&&h->preset==current.preset&&h->block==current.block&&h->width==current.width&&h->height==current.height&&h->image_crc==current.image_crc;}
static size_t respond(const image_header *q,uint32_t op,uint32_t s,uint32_t detail,uint8_t *out,size_t cap,int complete){
 image_header h=*q;h.op=op;h.session=session;h.state=s;h.detail=detail;h.length=complete?64u:0u;h.payload_crc=complete?image_crc32(metrics,64):0;
 if(cap<64u+h.length)return 0;image_header_encode(out,&h);if(complete)memcpy(out+64,metrics,64);return 64u+h.length;
}
static size_t reply(const image_header *q,uint32_t detail,uint8_t *out,size_t cap){return respond(q,IMAGE_REPLY,state,detail?detail:reason,out,cap,state==IMAGE_APPLIED&&has_metrics);}
void image_rx_poll(uint64_t now){if(state==IMAGE_RECEIVING&&(now<last||now<started||now-last>=15000u||now-started>=180000u)){state=IMAGE_FAILED;reason=2;pinned=0;}}
size_t image_rx_handle(const uint8_t *p,size_t n,uint32_t ip,uint16_t port,uint64_t now,uint8_t *out,size_t cap){
 image_header h;if(image_header_decode(&h,p,n))return 0;image_rx_poll(now);
 if(h.op==IMAGE_HELLO){image_header q=current;q.op=IMAGE_REPLY;q.session=session;q.tx=last_tx;q.input_id=active_id;q.image_crc=active_crc;q.state=state;q.detail=0;q.offset=0;q.length=bundle_ready?64u:0u;q.payload_crc=bundle_ready?image_crc32((const uint8_t*)bundle,64):0;if(cap<64+q.length)return 0;image_header_encode(out,&q);if(bundle_ready)memcpy(out+64,bundle,64);return 64+q.length;}
 if(h.op>IMAGE_STYLE||h.state||h.detail||h.session!=session||!h.tx)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
 int peer=ip==peer_ip&&port==peer_port;
 if(h.op==IMAGE_STATUS||h.op==IMAGE_CANCEL){
  if(h.length||!peer||h.tx!=current.tx)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
  if(h.op==IMAGE_CANCEL&&state==IMAGE_RECEIVING&&!pinned){state=IMAGE_FAILED;reason=3;}return reply(&current,0,out,cap);
 }
 if(h.op==IMAGE_BEGIN||h.op==IMAGE_STYLE){
  if(identity(&h)&&peer){
   if(h.op==IMAGE_BEGIN){if(h.length!=16||h.payload_crc!=image_crc32(p+64,16)||image_get32(p+64)!=rect.x||image_get32(p+68)!=rect.y||image_get32(p+72)!=rect.w||image_get32(p+76)!=rect.h)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);}
   else if(h.length)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);return reply(&h,0,out,cap);
  }
  if(pinned||state==IMAGE_RECEIVING||state==IMAGE_READY||state==IMAGE_PROCESSING)return respond(&h,IMAGE_REPLY,state,5,out,cap,0);
  if(h.tx<=last_tx||!image_wire_params(&h)||h.offset||!stage||capacity<IMAGE_BYTES)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
  image_rect next=rect;
  if(h.op==IMAGE_BEGIN){
   if(h.length!=16||h.payload_crc!=image_crc32(p+64,16)||!h.input_id||h.input_id<=active_id)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
   next=(image_rect){image_get32(p+64),image_get32(p+68),image_get32(p+72),image_get32(p+76)};
   if(!next.w||!next.h||next.x>=1280||next.y>=720||next.w>1280-next.x||next.h>720-next.y)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
  }else if(h.length||!active_id||h.input_id!=active_id||h.image_crc!=active_crc)return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
  current=h;rect=next;peer_ip=ip;peer_port=port;last_tx=h.tx;reason=received=0;has_metrics=pinned=0;new_image=h.op==IMAGE_BEGIN;started=last=now;
  if(new_image){memset(bitmap,0,sizeof(bitmap));state=IMAGE_RECEIVING;}else state=IMAGE_READY;return reply(&h,0,out,cap);
 }
 if(h.op==IMAGE_DATA){
  if(!new_image||!peer||!identity(&h)||h.offset>=IMAGE_BYTES||h.offset%1200u||!h.length||h.length!=(IMAGE_BYTES-h.offset<1200u?IMAGE_BYTES-h.offset:1200u))return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);
  if(h.payload_crc!=image_crc32(p+64,h.length))return respond(&h,IMAGE_REPLY,IMAGE_FAILED,1,out,cap,0);
  uint32_t chunk=h.offset/1200u,mask=1u<<(chunk&7u);
  if(bitmap[chunk/8u]&mask){if(memcmp(stage+h.offset,p+64,h.length))return respond(&h,IMAGE_REPLY,IMAGE_FAILED,4,out,cap,0);return respond(&h,IMAGE_ACK,state,reason,out,cap,state==IMAGE_APPLIED&&has_metrics);}
  if(state!=IMAGE_RECEIVING||pinned)return reply(&h,5,out,cap);
  memcpy(stage+h.offset,p+64,h.length);bitmap[chunk/8u]|=(uint8_t)mask;received+=h.length;last=now;
  if(received==IMAGE_BYTES){if(image_crc32(stage,IMAGE_BYTES)!=current.image_crc){state=IMAGE_FAILED;reason=1;}else state=IMAGE_READY;}
  return respond(&h,IMAGE_ACK,state,reason,out,cap,0);
 }
 return 0;
}
int image_rx_acquire(image_request *q){if(!q||state!=IMAGE_READY||pinned)return 0;pinned=1;state=IMAGE_PROCESSING;*q=(image_request){current.tx,current.input_id,current.image_crc,(uint32_t)new_image,rect,{current.style,current.preset,current.block},started};return 1;}
void image_rx_fail(uint32_t tx,uint32_t why){if(tx!=current.tx||!pinned)return;state=IMAGE_FAILED;reason=why;pinned=0;has_metrics=0;}
int image_rx_complete(uint32_t tx,const image_result *r,uint32_t before,uint32_t after,uint64_t hz,uint64_t total){
 if(!r||!pinned||state!=IMAGE_PROCESSING||tx!=current.tx||r->input_id!=current.input_id||r->style!=current.style||r->preset!=current.preset||r->block!=current.block||after-before!=r->pass_count)return -4;
 uint32_t v[16]={r->cache_hit,r->pass_count,before,after,(uint32_t)r->gpu_cycles,(uint32_t)(r->gpu_cycles>>32),(uint32_t)r->generate_ticks,(uint32_t)(r->generate_ticks>>32),(uint32_t)r->blit_ticks,(uint32_t)(r->blit_ticks>>32),(uint32_t)r->retire_ticks,(uint32_t)(r->retire_ticks>>32),(uint32_t)hz,(uint32_t)(hz>>32),(uint32_t)total,(uint32_t)(total>>32)};
 for(unsigned i=0;i<16;i++)image_put32(metrics+4*i,v[i]);has_metrics=1;state=IMAGE_APPLIED;reason=r->output_crc;pinned=0;return 0;
}
