#include <vxl/demo_hunet.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <cerrno>
static std::vector<uint8_t> read(const char *p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(int argc,char **argv){
 assert(argc==4);auto table=read(argv[1]),atlas=read(argv[2]),raw=read(argv[3]);
 demo_labels labels{};demo_hunet_result result{};
 assert(!demo_labels_bind(table.data(),table.size(),atlas.data(),atlas.size(),"ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f",nullptr,&labels));
 labels.bbox={40,12,87,105,1000,1000,0};
 std::vector<float> logits(DEMO_HUNET_CLASSES);assert(raw.size()==logits.size()*4);memcpy(logits.data(),raw.data(),raw.size());
 assert(!demo_hunet_decode(logits.data(),logits.size(),&labels,&result));
 const uint32_t expected[]={379,1,2744,1010,368};
 for(unsigned i=0;i<5;i++)assert(result.top[i].class_id==expected[i]);
 assert(!strcmp(result.top[0].utf8,"臣"));assert(result.bbox.x0==40&&result.bbox.x1==87);
 assert(!strcmp(result.logits_sha,"58a75990aad6711810065fa7a3c83e81a3eaaba13769f0395eac95dd7fa48654"));
 assert(!demo_hunet_classify(logits.data(),logits.data(),logits.size(),0,1,&result));
 assert(result.numeric_class==DEMO_NUMERIC_KNOWN_DEVIATION&&!result.ort_pass&&result.history_byte_equal);
 auto different=logits;different[0]+=1.0f;auto bound=result;
 assert(demo_hunet_classify(different.data(),different.data(),different.size(),0,1,&result)==-ESTALE);
 assert(!memcmp(&bound,&result,sizeof(result)));
 std::vector<float> tied(logits.size(),0);assert(!demo_hunet_decode(tied.data(),tied.size(),&labels,&result));
 for(unsigned i=0;i<5;i++)assert(result.top[i].class_id==i);
 tied[1]=NAN;assert(demo_hunet_decode(tied.data(),tied.size(),&labels,&result)<0);
 assert(demo_hunet_decode(logits.data(),logits.size()-1,&labels,&result)<0);
 labels.bbox.x0=-1;assert(demo_hunet_decode(logits.data(),logits.size(),&labels,&result)<0);labels.bbox.x0=40;
 labels.items[2].class_id=3;assert(demo_hunet_decode(logits.data(),logits.size(),&labels,&result)<0);
 demo_labels_close(&labels);puts("HUNET_HISTORICAL_RESULT_ADAPTER_HOST_PASS_NOT_INFERENCE");
}
