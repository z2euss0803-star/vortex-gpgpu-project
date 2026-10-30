#include "image/network/image_rx_linux.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
uint32_t host_ps_read(uint64_t a){(void)a;return 0;}
static vx_image_receiver*rx;static uint8_t packet[1280],reply[1280];
static image_header call(image_header*h,const uint8_t*payload,uint32_t n,uint32_t peer,uint16_t port,uint64_t now){
 h->length=n;h->payload_crc=n?image_crc32(payload,n):0;image_header_encode(packet,h);if(n)memcpy(packet+64,payload,n);size_t bytes=0;
 assert(!vx_image_receiver_handle(rx,packet,64u+n,peer,port,now,reply,sizeof(reply),&bytes)&&bytes>=64);image_header result;assert(!image_header_decode(&result,reply,bytes));return result;
}
static void*query_worker(void*p){
 const uint32_t expected=(uint32_t)(uintptr_t)p;
 for(unsigned i=0;i<250;i++){
  uint8_t in[64],out[128];image_header h={.op=IMAGE_HELLO};image_header_encode(in,&h);size_t bytes=0;
  assert(!vx_image_receiver_handle(rx,in,64,11,22,i,out,sizeof(out),&bytes)&&bytes==128);image_header got;assert(!image_header_decode(&got,out,bytes)&&got.state==expected&&got.session==42);
  vx_image_receiver*other=0;assert(vx_image_receiver_create(50,"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3",100,1,&other)==-EBUSY&&!other);
 }
 return 0;
}
static void query_parallel(uint32_t expected){pthread_t workers[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(workers+i,0,query_worker,(void*)(uintptr_t)expected));for(unsigned i=0;i<4;i++)assert(!pthread_join(workers[i],0));}
int main(void){
 assert(VX_IMAGE_UDP_PORT==5011&&VX_IMAGE_UDP_WINDOW==8&&IMAGE_CHUNK_BYTES==1200&&IMAGE_HEADER_BYTES==64&&IMAGE_APPLIED==4);
 const char*bundle="20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3";
 int rc=vx_image_receiver_create(42,bundle,100,1,&rx);if(rc){fprintf(stderr,"U15_RX_RED frozen UDP receiver unavailable rc=%d\n",rc);return 83;}
 vx_image_receiver*second=0;assert(vx_image_receiver_create(43,bundle,200,1,&second)==-EBUSY&&!second);
 query_parallel(IMAGE_IDLE);
 uint8_t*source=malloc(IMAGE_BYTES);assert(source);memset(source,255,IMAGE_BYTES);uint32_t crc=image_crc32(source,IMAGE_BYTES);
 image_header h={.op=IMAGE_HELLO};image_header r=call(&h,0,0,11,22,0);assert(r.session==42&&r.state==IMAGE_IDLE&&r.length==64);
 h=(image_header){.op=IMAGE_BEGIN,.session=42,.tx=1,.input_id=1,.style=1,.preset=1,.width=1280,.height=720,.image_crc=crc};uint8_t rect[16];image_put32(rect,0);image_put32(rect+4,0);image_put32(rect+8,1280);image_put32(rect+12,720);r=call(&h,rect,16,11,22,1);assert(r.state==IMAGE_RECEIVING);
 h.op=IMAGE_DATA;h.offset=0;r=call(&h,source,1200,11,22,2);assert(r.op==IMAGE_ACK);r=call(&h,source,1200,11,22,3);assert(r.op==IMAGE_ACK);
 uint8_t changed[1200];memset(changed,7,sizeof(changed));r=call(&h,changed,1200,11,22,4);assert(r.state==IMAGE_FAILED&&r.detail==4);r=call(&h,source,1200,99,22,5);assert(r.state==IMAGE_FAILED&&r.detail==4);
 /* Reverse order exercises eight-packet-sized groups, omitted chunk and final
  * partial fragment without replacing the original sender's sliding window. */
 const unsigned chunks=(IMAGE_BYTES+1199)/1200;for(unsigned k=chunks-1;k>0;k--){h.offset=k*1200;unsigned n=IMAGE_BYTES-h.offset;if(n>1200)n=1200;r=call(&h,source+h.offset,n,11,22,10+chunks-k);}
 assert(r.state==IMAGE_READY);vx_image_ingress q;assert(vx_image_receiver_acquire(rx,&q)==1&&q.serial==1&&q.display_cookie==101&&q.wire.new_image&&q.bytes==IMAGE_BYTES&&image_crc32(q.rgba,q.bytes)==crc);assert(!vx_image_receiver_acquire(rx,&q));assert(vx_image_receiver_close(rx)==-EBUSY);
 h.offset=0;r=call(&h,changed,1200,11,22,4000);assert(r.state==IMAGE_FAILED&&r.detail==4);assert(image_crc32(q.rgba,q.bytes)==crc);h.op=IMAGE_CANCEL;r=call(&h,0,0,11,22,4001);assert(r.state==IMAGE_PROCESSING);assert(!vx_image_receiver_poll(rx,1000000));
 query_parallel(IMAGE_PROCESSING);
 assert(!vx_image_receiver_admitted(rx,q.serial));vx_image_rx_completion done={.serial=1,.display_cookie=101,.total_ns=1000,.output={.input_id=1,.style=1,.preset=1,.output_crc=123,.pass_count=5,.generate_ticks=300,.blit_ticks=100,.retire_ticks=100},.jobs_before=0,.jobs_after=5,.scope=0,.cycles_valid=0,.execution_complete=1};vx_image_rx_display dp={.cookie=101,.had_old_front=1,.submitted_ns=10,.presented_ns=20,.old_retired_ns=30,.scope=0,.clock_domain=1,.presented=1,.old_retired=1};
 vx_image_rx_display bad=dp;bad.presented=0;assert(vx_image_receiver_complete(rx,&done,&bad)<0);bad=dp;bad.old_retired=0;assert(vx_image_receiver_complete(rx,&done,&bad)<0);bad=dp;bad.cookie++;assert(vx_image_receiver_complete(rx,&done,&bad)<0);bad=dp;bad.presented_ns=9;assert(vx_image_receiver_complete(rx,&done,&bad)<0);bad=dp;bad.scope=4;assert(vx_image_receiver_complete(rx,&done,&bad)<0);
 h.op=IMAGE_STATUS;r=call(&h,0,0,11,22,1000001);assert(r.state==IMAGE_PROCESSING&&r.length==0);done.output.gpu_cycles=1;assert(vx_image_receiver_complete(rx,&done,&dp)<0);done.output.gpu_cycles=0;assert(!vx_image_receiver_complete(rx,&done,&dp));r=call(&h,0,0,11,22,1000002);assert(r.state==IMAGE_APPLIED&&r.detail==123&&r.length==64);assert(image_get32(reply+64+12*4)==1000000000u);
 h.op=IMAGE_DATA;h.offset=0;r=call(&h,source,1200,11,22,1000002);if(r.op==IMAGE_ACK&&r.state==IMAGE_APPLIED){fprintf(stderr,"U15_LATE_ACK_RED op=%u state=%u length=%u\n",r.op,r.state,r.length);return 84;}assert(r.op==IMAGE_ACK&&r.state==IMAGE_PROCESSING&&r.length==0);h.op=IMAGE_STATUS;r=call(&h,0,0,11,22,1000002);assert(r.state==IMAGE_APPLIED&&r.length==64);
 h.op=IMAGE_STYLE;h.tx=2;h.style=4;h.preset=0;h.block=4;r=call(&h,0,0,11,22,1000003);assert(r.state==IMAGE_READY);assert(vx_image_receiver_acquire(rx,&q)==1&&!q.wire.new_image&&q.serial==2&&image_crc32(q.rgba,q.bytes)==crc);assert(!vx_image_receiver_fail(rx,q.serial,6));
 h.op=IMAGE_BEGIN;h.tx=3;h.input_id=2;h.style=1;h.block=0;r=call(&h,rect,16,11,22,1000010);assert(r.state==IMAGE_RECEIVING);assert(!vx_image_receiver_poll(rx,1000009));h.op=IMAGE_STATUS;r=call(&h,0,0,11,22,1000011);assert(r.state==IMAGE_FAILED&&r.detail==2);h.op=IMAGE_BEGIN;h.tx=4;h.input_id=3;r=call(&h,rect,16,11,22,1100000);assert(r.state==IMAGE_RECEIVING);assert(!vx_image_receiver_poll(rx,1114999));h.op=IMAGE_STATUS;r=call(&h,0,0,11,22,1114999);assert(r.state==IMAGE_RECEIVING);assert(!vx_image_receiver_poll(rx,1115000));r=call(&h,0,0,11,22,1115000);assert(r.state==IMAGE_FAILED&&r.detail==2);
 h.op=IMAGE_BEGIN;h.tx=5;h.input_id=4;r=call(&h,rect,16,11,22,2000000);assert(r.state==IMAGE_RECEIVING);h.op=IMAGE_DATA;
 for(unsigned k=0;k<18;k++){h.offset=k*1200;r=call(&h,source+h.offset,1200,11,22,2000000+k*10000);assert(r.state==IMAGE_RECEIVING);}
 assert(!vx_image_receiver_poll(rx,2179999));h.op=IMAGE_STATUS;r=call(&h,0,0,11,22,2179999);assert(r.state==IMAGE_RECEIVING);assert(!vx_image_receiver_poll(rx,2180000));r=call(&h,0,0,11,22,2180000);assert(r.state==IMAGE_FAILED&&r.detail==2);assert(!vx_image_receiver_close(rx));free(source);
 puts("U15_IMAGE_RX_ORIGINAL_WIRE_PINNED_INPUT_DP_GATED_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;
}
