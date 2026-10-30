#include <string.h>
#include "image_protocol.h"
void image_put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
uint32_t image_get32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
void image_header_encode(uint8_t *p,const image_header *h){
 memset(p,0,64);image_put32(p,IMAGE_MAGIC);p[4]=1;p[5]=(uint8_t)h->op;
 const uint32_t values[14]={h->session,h->tx,h->input_id,h->style,h->preset,h->block,h->width,h->height,h->offset,h->length,h->payload_crc,h->image_crc,h->state,h->detail};
 for(unsigned i=0;i<14;i++)image_put32(p+8+4*i,values[i]);
}
int image_header_decode(image_header *h,const uint8_t *p,size_t n){
 if(!h||!p||n<64||image_get32(p)!=IMAGE_MAGIC||p[4]!=1||p[6]||p[7]||p[5]<1||p[5]>8)return -4;
 uint32_t v[14];for(unsigned i=0;i<14;i++)v[i]=image_get32(p+8+4*i);
 *h=(image_header){p[5],v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13]};
 if(h->length>1200u||n!=64u+h->length)return -4;return 0;
}
int image_wire_params(const image_header *h){return h->width==1280&&h->height==720&&h->style<=4&&h->preset<=6&&(!(h->style==0||h->style==2)||h->preset==0)&&(h->style==4?(h->block==2||h->block==4||h->block==8):h->block==0);}
