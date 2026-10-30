#include <vxl/demo_hunet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
static uint16_t le16(const uint8_t *p){return static_cast<uint16_t>(p[0])|static_cast<uint16_t>(p[1])<<8;}
static uint32_t le32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static void hex32(const uint8_t *p,char out[65]){static const char digits[]="0123456789abcdef";for(unsigned i=0;i<32;i++){out[2*i]=digits[p[i]>>4];out[2*i+1]=digits[p[i]&15];}out[64]=0;}
static bool utf8(uint32_t cp,char out[8]){
 memset(out,0,8);
 if(cp<32||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
 if(cp<128)out[0]=char(cp);
 else if(cp<2048){out[0]=char(0xc0|(cp>>6));out[1]=char(0x80|(cp&63));}
 else if(cp<65536){out[0]=char(0xe0|(cp>>12));out[1]=char(0x80|((cp>>6)&63));out[2]=char(0x80|(cp&63));}
 else {out[0]=char(0xf0|(cp>>18));out[1]=char(0x80|((cp>>12)&63));out[2]=char(0x80|((cp>>6)&63));out[3]=char(0x80|(cp&63));}
 return true;
}
int demo_labels_bind(const void *table,size_t n,const uint8_t *atlas,size_t atlas_bytes,const char *classes_sha,const char *font_sha,demo_labels *out){
 const auto *p=static_cast<const uint8_t *>(table);char class_hex[65],font_hex[65];
 if(!p||!atlas||!out||out->items||!classes_sha||strlen(classes_sha)!=64||n!=96+DEMO_HUNET_CLASSES*32||atlas_bytes!=DEMO_HUNET_CLASSES*1024)return -EINVAL;
 if(memcmp(p,"DMLAB1\0\0",8)||le32(p+8)!=1||le32(p+12)!=DEMO_HUNET_CLASSES||le32(p+16)!=32||le32(p+20)!=32||le32(p+24)!=32||le32(p+28))return -EINVAL;
 hex32(p+32,class_hex);hex32(p+64,font_hex);
 if(strcmp(class_hex,classes_sha)||(font_sha&&strcmp(font_hex,font_sha)))return -ESTALE;
 auto *items=static_cast<demo_label *>(calloc(DEMO_HUNET_CLASSES,sizeof(demo_label)));if(!items)return -ENOMEM;
 for(unsigned i=0;i<DEMO_HUNET_CLASSES;i++){
  const uint8_t *r=p+96+32*i;auto &entry=items[i];char encoded[8];
  entry.class_id=le32(r);entry.codepoint=le32(r+4);entry.glyph_offset=le32(r+8);entry.width=le16(r+12);entry.height=le16(r+14);entry.stride=le16(r+16);entry.available=le16(r+18);memcpy(entry.utf8,r+20,8);
  bool valid=entry.class_id==i&&entry.glyph_offset==i*1024&&entry.width==32&&entry.height==32&&entry.stride==32&&entry.available<=1&&!le32(r+28)&&utf8(entry.codepoint,encoded)&&!memcmp(entry.utf8,encoded,8);
  bool any=false;for(unsigned j=0;j<1024;j++)any|=atlas[i*1024+j]!=0;
  if(!valid||any!=bool(entry.available)){free(items);return -EINVAL;}
 }
 memset(out,0,sizeof(*out));out->items=items;out->count=DEMO_HUNET_CLASSES;out->atlas=atlas;out->atlas_bytes=atlas_bytes;
 strcpy(out->classes_sha,class_hex);strcpy(out->font_sha,font_hex);demo_sha_hex(atlas,atlas_bytes,out->glyph_sha);return 0;
}
void demo_labels_close(demo_labels *labels){if(labels){free(labels->items);memset(labels,0,sizeof(*labels));}}
static bool valid_labels(const demo_labels *labels){
 if(!labels||!labels->items||labels->count!=DEMO_HUNET_CLASSES)return false;
 const auto &b=labels->bbox;
 if(b.x0<0||b.y0<0||b.x1<=b.x0||b.y1<=b.y0||!b.image_width||!b.image_height||uint64_t(b.x1)>b.image_width||uint64_t(b.y1)>b.image_height)return false;
 for(unsigned i=0;i<DEMO_HUNET_CLASSES;i++){
  char encoded[8];const auto &entry=labels->items[i];
  if(entry.class_id!=i||!utf8(entry.codepoint,encoded)||memcmp(entry.utf8,encoded,8))return false;
 }
 return true;
}
int demo_hunet_decode(const float *logits,size_t count,const demo_labels *labels,demo_hunet_result *out){
 if(!out)return -EINVAL;
 memset(out,0,sizeof(*out));
 if(!logits||count!=DEMO_HUNET_CLASSES||!valid_labels(labels))return -EINVAL;
 for(size_t i=0;i<count;i++)if(!std::isfinite(logits[i]))return -EDOM;
 for(unsigned i=0;i<DEMO_HUNET_TOPK;i++)out->top[i].class_id=UINT32_MAX;
 for(uint32_t id=0;id<count;id++){
  for(unsigned rank=0;rank<DEMO_HUNET_TOPK;rank++){
   const auto &old=out->top[rank];
   if(old.class_id==UINT32_MAX||logits[id]>old.score||(logits[id]==old.score&&id<old.class_id)){
    for(unsigned j=DEMO_HUNET_TOPK-1;j>rank;j--)out->top[j]=out->top[j-1];
    out->top[rank].class_id=id;out->top[rank].score=logits[id];memcpy(out->top[rank].utf8,labels->items[id].utf8,8);break;
   }
  }
 }
 out->bbox=labels->bbox;strcpy(out->classes_sha,labels->classes_sha);strcpy(out->glyph_sha,labels->glyph_sha);
 for(unsigned rank=0;rank<DEMO_HUNET_TOPK;rank++){
  int rc=demo_glyph_lookup(out->top[rank].class_id,labels,&out->glyphs[rank]);
  if(rc){memset(out,0,sizeof(*out));return rc;}
 }
 demo_sha_hex(logits,count*sizeof(float),out->logits_sha);out->completed=1;return 0;
}
int demo_hunet_classify(const float *actual,const float *historical,size_t count,int ort_pass,int r2_pass,demo_hunet_result *out){
 if(!actual||!historical||!out||count!=DEMO_HUNET_CLASSES||!out->completed||(ort_pass!=0&&ort_pass!=1)||(r2_pass!=0&&r2_pass!=1))return -EINVAL;
 for(size_t i=0;i<count;i++)if(!std::isfinite(actual[i])||!std::isfinite(historical[i]))return -EDOM;
 char actual_sha[65];demo_sha_hex(actual,count*sizeof(float),actual_sha);
 if(strcmp(actual_sha,out->logits_sha))return -ESTALE;
 out->ort_evaluated=out->r2_evaluated=1;out->ort_pass=ort_pass;out->r2_pass=r2_pass;
 out->history_byte_equal=!memcmp(actual,historical,count*sizeof(float));
 out->numeric_class=ort_pass&&r2_pass?DEMO_NUMERIC_PASS:out->history_byte_equal?DEMO_NUMERIC_KNOWN_DEVIATION:DEMO_NUMERIC_FAIL;
 return 0;
}
