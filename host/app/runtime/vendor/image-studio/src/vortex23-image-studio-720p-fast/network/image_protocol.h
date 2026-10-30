#ifndef IMAGE_PROTOCOL_H
#define IMAGE_PROTOCOL_H
#include <stddef.h>
#include "image_pipeline.h"
#define IMAGE_MAGIC UINT32_C(0x56534934)
#define IMAGE_HEADER_BYTES 64u
#define IMAGE_CHUNK_BYTES 1200u
#define IMAGE_PACKET_MAX 1280u
enum {IMAGE_HELLO=1,IMAGE_BEGIN=2,IMAGE_DATA=3,IMAGE_STATUS=4,IMAGE_CANCEL=5,IMAGE_STYLE=6,IMAGE_ACK=7,IMAGE_REPLY=8};
enum {IMAGE_IDLE=0,IMAGE_RECEIVING=1,IMAGE_READY=2,IMAGE_PROCESSING=3,IMAGE_APPLIED=4,IMAGE_FAILED=5};
typedef struct {uint32_t op,session,tx,input_id,style,preset,block,width,height,offset,length,payload_crc,image_crc,state,detail;} image_header;
void image_put32(uint8_t *p,uint32_t v);
uint32_t image_get32(const uint8_t *p);
void image_header_encode(uint8_t *out,const image_header *h);
int image_header_decode(image_header *h,const uint8_t *p,size_t n);
int image_wire_params(const image_header *h);
#endif
