#include <vxl/demo_hunet.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
int demo_glyph_lookup(uint32_t id,const demo_labels *labels,demo_glyph *out){
 if(!out)return -EINVAL;
 memset(out,0,sizeof(*out));
 if(!labels||!labels->items||labels->count!=DEMO_HUNET_CLASSES||id>=labels->count||labels->items[id].class_id!=id)return -EINVAL;
 const auto &entry=labels->items[id];out->class_id=id;
 if(!entry.available){snprintf(out->fallback,sizeof(out->fallback),"#%u",id);return 0;}
 uint64_t bytes=uint64_t(entry.height)*entry.stride;
 if(entry.available!=1||!entry.width||!entry.height||entry.width>entry.stride||!labels->atlas||entry.glyph_offset>labels->atlas_bytes||bytes>labels->atlas_bytes-entry.glyph_offset)return -ERANGE;
 out->width=entry.width;out->height=entry.height;out->stride=entry.stride;out->pixels=labels->atlas+entry.glyph_offset;out->available=1;return 0;
}
