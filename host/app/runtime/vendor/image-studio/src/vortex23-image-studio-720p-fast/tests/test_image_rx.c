#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/image_rx.h"
uint32_t host_ps_read(uint64_t a){(void)a;return 0;}
static uint8_t packet[1280],reply[1280];
static image_header call(image_header *h,const uint8_t *payload,uint32_t n,uint64_t now){
 h->length=n;h->payload_crc=n?image_crc32(payload,n):0;image_header_encode(packet,h);if(n)memcpy(packet+64,payload,n);
 size_t size=image_rx_handle(packet,64u+n,11,22,now,reply,sizeof(reply));assert(size>=64);image_header r;assert(!image_header_decode(&r,reply,size));return r;
}
int main(void){
 uint8_t *storage=malloc(IMAGE_BYTES+128),*source=malloc(IMAGE_BYTES);memset(storage,0xa5,IMAGE_BYTES+128);memset(source,255,IMAGE_BYTES);
 image_rx_init(storage+64,IMAGE_BYTES,42);image_header h={.op=IMAGE_BEGIN,.session=42,.tx=1,.input_id=1,.style=1,.preset=1,.width=1280,.height=720,.image_crc=image_crc32(source,IMAGE_BYTES)};
 uint8_t meta[16];image_put32(meta,0);image_put32(meta+4,0);image_put32(meta+8,1280);image_put32(meta+12,720);
 image_header r=call(&h,meta,16,0);assert(r.state==IMAGE_RECEIVING);
 h.op=IMAGE_DATA;h.offset=UINT32_MAX;r=call(&h,source,1200,1);assert(r.detail==4);assert(storage[0]==0xa5&&storage[IMAGE_BYTES+127]==0xa5);
 h.offset=0;r=call(&h,source,1200,2);assert(r.op==IMAGE_ACK);r=call(&h,source,1200,3);assert(r.op==IMAGE_ACK);
 for(unsigned chunk=IMAGE_BYTES/1200u-1u;chunk>0;chunk--){unsigned offset=chunk*1200u;h.offset=offset;unsigned n=IMAGE_BYTES-offset;if(n>1200)n=1200;r=call(&h,source+offset,n,4+(IMAGE_BYTES/1200u-1u-chunk));}
 assert(r.state==IMAGE_READY);image_request request;assert(image_rx_acquire(&request));assert(!image_rx_acquire(&request));
 h.op=IMAGE_CANCEL;h.offset=0;r=call(&h,NULL,0,1000);assert(r.state==IMAGE_PROCESSING);
 image_rx_poll(1000000);h.op=IMAGE_STATUS;r=call(&h,NULL,0,1000000);assert(r.state==IMAGE_PROCESSING);
 image_rx_set_active(1,h.image_crc);image_result result={.input_id=1,.style=1,.preset=1,.output_crc=123,.pass_count=5};assert(!image_rx_complete(1,&result,0,5,1000,10));
 r=call(&h,NULL,0,1000001);assert(r.state==IMAGE_APPLIED&&r.detail==123&&r.length==64);
 h.style=3;r=call(&h,NULL,0,1000001);assert(r.style==1&&r.preset==1);h.style=1;
 h.op=IMAGE_DATA;h.offset=0;h.length=1200;h.payload_crc=0;image_header_encode(packet,&h);memcpy(packet+64,source,1200);
 assert(image_rx_handle(packet,1264,11,22,1000001,reply,sizeof(reply))>=64);assert(!image_header_decode(&r,reply,64));assert(r.state==IMAGE_FAILED&&r.detail==1);
 h.op=IMAGE_STYLE;h.tx=2;h.style=4;h.preset=0;h.block=4;r=call(&h,NULL,0,1000002);assert(r.state==IMAGE_READY);r=call(&h,NULL,0,1000003);assert(r.state==IMAGE_READY);assert(image_rx_acquire(&request));assert(!request.new_image);
 h.tx=3;h.op=IMAGE_BEGIN;r=call(&h,meta,16,1000004);assert(r.state==IMAGE_PROCESSING&&r.detail==5);
 image_rx_fail(2,6);h.tx=3;h.input_id=2;h.style=1;h.block=0;r=call(&h,meta,16,1000005);assert(r.state==IMAGE_RECEIVING);image_rx_poll(1016000);h.op=IMAGE_STATUS;r=call(&h,NULL,0,1016000);assert(r.state==IMAGE_FAILED&&r.detail==2);
 for(unsigned i=0;i<64;i++){assert(storage[i]==0xa5);assert(storage[IMAGE_BYTES+64+i]==0xa5);}puts("IMAGE_PROTOCOL_OWNER_BOUNDS_PASS");free(storage);free(source);return 0;
}
