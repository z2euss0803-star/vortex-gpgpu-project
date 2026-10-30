#include <vxl/demo_hunet.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
int main(){
 std::vector<demo_label> items(DEMO_HUNET_CLASSES);demo_labels labels{};labels.items=items.data();labels.count=items.size();
 for(unsigned i=0;i<items.size();i++)items[i].class_id=i;
 demo_glyph glyph{};assert(!demo_glyph_lookup(379,&labels,&glyph));
 assert(!glyph.available&&!strcmp(glyph.fallback,"#379"));assert(!glyph.pixels);
 assert(demo_glyph_lookup(8105,&labels,&glyph)<0);
 uint8_t pixel=255;labels.atlas=&pixel;labels.atlas_bytes=1;
 items[379].width=items[379].height=items[379].stride=1;items[379].available=1;
 assert(!demo_glyph_lookup(379,&labels,&glyph));assert(glyph.available&&glyph.pixels[0]==255);
 items[379].glyph_offset=1;assert(demo_glyph_lookup(379,&labels,&glyph)<0);
 puts("HUNET_GLYPH_MISSING_AND_BOUNDS_HOST_PASS_NOT_DP");
}
