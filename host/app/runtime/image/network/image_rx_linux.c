#include "image_rx_linux.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
/* Keep the original static receiver unchanged, with exactly one process-wide
 * protected instance. Only the internal Linux wrapper may call it. */
/* Byte-only CRC32 is the frozen image_crc.c polynomial/table algorithm. It
 * accepts owned CPU bytes only; the bare-metal image_crc_region/host_ps_read
 * helper is deliberately not linked into the Linux service. All calls are
 * serialized by receiver_mutex, including initialization. */
static uint32_t wire_crc32(const uint8_t*p,uint32_t bytes){
 static uint32_t table[256];static int ready;
 if(!ready){for(uint32_t i=0;i<256;i++){uint32_t c=i;for(unsigned k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);table[i]=c;}ready=1;}
 uint32_t c=~0u;for(uint32_t i=0;i<bytes;i++)c=(c>>8)^table[(c^p[i])&255u];return ~c;
}
#define image_crc32 wire_crc32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/network/image_rx.c"
#pragma GCC diagnostic pop
#undef image_crc32
struct vx_image_receiver {
 uint8_t *guarded,*active;
 uint64_t serial,display_base;
 uint32_t active_known,admitted,allow_host;
 int fault;
 vx_image_ingress ingress;
};
static pthread_mutex_t receiver_mutex=PTHREAD_MUTEX_INITIALIZER;
static vx_image_receiver *receiver_owner;
static int unlock_result(int rc){pthread_mutex_unlock(&receiver_mutex);return rc;}
static int valid(vx_image_receiver*x){
 if(!x||x!=receiver_owner)return -ESTALE;
 if(x->fault)return x->fault;
 for(unsigned i=0;i<64;i++)if(x->guarded[i]!=0xa5||x->guarded[64u+IMAGE_BYTES+i]!=0xa5){x->fault=-EBADMSG;return x->fault;}
 return 0;
}
static int pending_request(vx_image_receiver*x,uint64_t serial){
 int rc=valid(x);if(rc)return rc;
 if(!serial||serial!=x->ingress.serial||!pinned||state!=IMAGE_PROCESSING)return -ESTALE;
 return 0;
}
int vx_image_receiver_create(uint32_t boot,const char*b,uint64_t base,int allow_host,vx_image_receiver**out){
 if(!out)return -EINVAL;
 *out=0;
 if(!boot||!b||!base||(allow_host!=0&&allow_host!=1))return -EINVAL;
 for(unsigned i=0;i<64;i++)if(!((b[i]>='0'&&b[i]<='9')||(b[i]>='a'&&b[i]<='f')))return -EINVAL;
 if(b[64])return -EINVAL;
 pthread_mutex_lock(&receiver_mutex);
 if(receiver_owner)return unlock_result(-EBUSY);
 vx_image_receiver*x=calloc(1,sizeof(*x));if(!x)return unlock_result(-ENOMEM);
 x->guarded=malloc(IMAGE_BYTES+128u);x->active=malloc(IMAGE_BYTES);
 if(!x->guarded||!x->active){free(x->guarded);free(x->active);free(x);return unlock_result(-ENOMEM);}
 memset(x->guarded,0xa5,IMAGE_BYTES+128u);memset(x->active,0,IMAGE_BYTES);
 x->display_base=base;x->allow_host=(uint32_t)allow_host;
 image_rx_init(x->guarded+64,IMAGE_BYTES,boot);image_rx_set_bundle(b);receiver_owner=x;*out=x;
 return unlock_result(0);
}
int vx_image_receiver_handle(vx_image_receiver*x,const uint8_t*p,size_t n,uint32_t ip,uint16_t port,uint64_t now,uint8_t*out,size_t cap,size_t*bytes){
 if(!bytes)return -EINVAL;
 *bytes=0;
 if(!p||!out||n>IMAGE_PACKET_MAX||cap<IMAGE_HEADER_BYTES)return -EINVAL;
 pthread_mutex_lock(&receiver_mutex);int rc=valid(x);if(rc)return unlock_result(rc);
 *bytes=image_rx_handle(p,n,ip,port,now,out,cap);
 /* Completion is a REPLY/64B metrics transaction, never a DATA ACK. The
  * unchanged PC sender may still have delayed ACKs queued at finish(). Keep
  * its sliding window ACKable without advertising a second completion. */
 if(*bytes>=IMAGE_HEADER_BYTES){
  image_header h;
  if(!image_header_decode(&h,out,*bytes)&&h.op==IMAGE_ACK&&h.state==IMAGE_APPLIED){
   h.state=IMAGE_PROCESSING;h.detail=0;h.length=0;h.payload_crc=0;
   image_header_encode(out,&h);*bytes=IMAGE_HEADER_BYTES;
  }
 }
 rc=valid(x);return unlock_result(rc);
}
int vx_image_receiver_poll(vx_image_receiver*x,uint64_t now){
 pthread_mutex_lock(&receiver_mutex);int rc=valid(x);if(rc)return unlock_result(rc);
 image_rx_poll(now);return unlock_result(valid(x));
}
int vx_image_receiver_acquire(vx_image_receiver*x,vx_image_ingress*out){
 if(!out)return -EINVAL;
 pthread_mutex_lock(&receiver_mutex);int rc=valid(x);if(rc)return unlock_result(rc);
 if(state!=IMAGE_READY||pinned)return unlock_result(0);
 if(x->serial==UINT64_MAX||x->serial+1>UINT64_MAX-x->display_base)return unlock_result(-EOVERFLOW);
 image_request q;if(!image_rx_acquire(&q))return unlock_result(0);
 if(!q.new_image&&!x->active_known){image_rx_fail(q.tx,4);return unlock_result(-ESTALE);}
 memset(&x->ingress,0,sizeof(x->ingress));x->ingress.wire=q;x->ingress.rgba=q.new_image?stage:x->active;x->ingress.bytes=IMAGE_BYTES;x->ingress.peer_ip=peer_ip;x->ingress.peer_port=peer_port;x->ingress.serial=++x->serial;x->ingress.display_cookie=x->display_base+x->serial;x->admitted=0;*out=x->ingress;
 return unlock_result(1);
}
int vx_image_receiver_admitted(vx_image_receiver*x,uint64_t serial){
 pthread_mutex_lock(&receiver_mutex);int rc=pending_request(x,serial);if(rc)return unlock_result(rc);
 if(x->admitted)return unlock_result(-EALREADY);
 if(wire_crc32(x->ingress.rgba,IMAGE_BYTES)!=x->ingress.wire.image_crc)return unlock_result(-EBADMSG);
 if(x->ingress.wire.new_image){memcpy(x->active,x->ingress.rgba,IMAGE_BYTES);x->active_known=1;image_rx_set_active(x->ingress.wire.input_id,x->ingress.wire.image_crc);}
 x->admitted=1;return unlock_result(0);
}
int vx_image_receiver_complete(vx_image_receiver*x,const vx_image_rx_completion*c,const vx_image_rx_display*d){
 if(!c||!d)return -EINVAL;
 pthread_mutex_lock(&receiver_mutex);int rc=pending_request(x,c->serial);if(rc)return unlock_result(rc);
 if(!x->admitted||c->execution_complete!=1||c->scope!=d->scope||(c->scope!=4&&c->scope!=0)||(!x->allow_host&&c->scope!=4))return unlock_result(-EPROTO);
 if(c->display_cookie!=x->ingress.display_cookie||d->cookie!=c->display_cookie||!d->cookie||d->presented!=1||d->clock_domain!=(c->scope==4?0u:1u)||!d->submitted_ns||!d->presented_ns||d->presented_ns<d->submitted_ns)return unlock_result(-EPROTO);
 /* The original KMS engine retires its initial blank even when no previous
  * producer result exists. Keep that real event; a producer front still MUST
  * retire, and any reported retirement must have a valid ordered timestamp. */
 if(d->had_old_front>1||d->old_retired>1)return unlock_result(-EPROTO);
 if(d->had_old_front&&!d->old_retired)return unlock_result(-EPROTO);
 if(d->old_retired){if(!d->old_retired_ns||d->old_retired_ns<d->presented_ns)return unlock_result(-EPROTO);}
 else if(d->old_retired_ns)return unlock_result(-EPROTO);
 const image_result*r=&c->output;
 if(c->cycles_valid>1||r->cache_hit>1||r->pass_count>VX_IMAGE_MAX_PASSES||c->jobs_after-c->jobs_before!=r->pass_count||(!r->pass_count&&r->style&&!r->cache_hit))return unlock_result(-EPROTO);
 if(c->scope==0){if(c->cycles_valid||r->gpu_cycles)return unlock_result(-EPROTO);}
 else if(r->pass_count?(!c->cycles_valid||!r->gpu_cycles):(c->cycles_valid||r->gpu_cycles))return unlock_result(-ENODATA);
 if(r->generate_ticks>c->total_ns||r->blit_ticks>c->total_ns||r->retire_ticks>c->total_ns)return unlock_result(-ERANGE);
 rc=image_rx_complete(x->ingress.wire.tx,r,c->jobs_before,c->jobs_after,1000000000ULL,c->total_ns);
 return unlock_result(rc);
}
int vx_image_receiver_fail(vx_image_receiver*x,uint64_t serial,uint32_t why){
 if(!why||why>6)return -EINVAL;
 pthread_mutex_lock(&receiver_mutex);int rc=pending_request(x,serial);if(rc)return unlock_result(rc);
 image_rx_fail(x->ingress.wire.tx,why);return unlock_result(0);
}
int vx_image_receiver_close(vx_image_receiver*x){
 pthread_mutex_lock(&receiver_mutex);int rc=valid(x);if(rc)return unlock_result(rc);
 if(pinned||state==IMAGE_RECEIVING||state==IMAGE_READY||state==IMAGE_PROCESSING)return unlock_result(-EBUSY);
 receiver_owner=0;stage=0;capacity=0;free(x->guarded);free(x->active);free(x);return unlock_result(0);
}
int vx_image_receiver_quiesce_unadmitted(vx_image_receiver*x){
 pthread_mutex_lock(&receiver_mutex);int rc=valid(x);if(rc)return unlock_result(rc);
 if(pinned&&x->admitted)return unlock_result(-EBUSY);
 if(state==IMAGE_RECEIVING&&!pinned){
  image_header h=current;uint8_t packet[64],reply_buffer[1280];
  h.op=IMAGE_CANCEL;h.length=h.payload_crc=h.state=h.detail=0;
  image_header_encode(packet,&h);
  (void)image_rx_handle(packet,sizeof(packet),peer_ip,peer_port,last,reply_buffer,sizeof(reply_buffer));
 }
 if(state==IMAGE_READY&&!pinned){image_request request;if(!image_rx_acquire(&request))return unlock_result(-EPROTO);}
 if(pinned)image_rx_fail(current.tx,3);
 if(pinned||state==IMAGE_RECEIVING||state==IMAGE_READY||state==IMAGE_PROCESSING)return unlock_result(-EBUSY);
 return unlock_result(valid(x));
}
