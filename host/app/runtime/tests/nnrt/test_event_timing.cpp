#include <vxnn/evidence.hpp>
#include <cassert>
#include <cstdio>
#include <string>
#include <cstring>
#include <json-c/json.h>
using namespace vxnn;
int main(int argc,char **argv){
 assert(argc==2);std::string mode=argv[1];Event parent;parent.host="hostA";parent.event_id=1;parent.start_ns=0;parent.end_ns=100;
 Event child=parent;child.event_id=2;child.parent_event=1;child.start_ns=10;child.end_ns=40;
 if(mode=="offline"){
  FILE *f=tmpfile();assert(f);FileEvidenceSink sink(fileno(f));child.cycles_valid=true;child.raw_cycles=123;
  assert(!sink.emit(child));assert(ftell(f)==0);fclose(f);
 }else if(mode=="nested"){
  auto result=summarize_timing({parent,child});assert(result&&result.value->exclusive_total_ns==100&&result.value->wall_ns==100);
 }else if(mode=="cross-host"){
  child.host="hostB";auto result=summarize_timing({parent,child});assert(!result);
 }else if(mode=="writer"){
  FILE *f=tmpfile();assert(f);FileEvidenceSink sink(fileno(f));parent.bytes=9007199254740993ULL;
  assert(sink.emit(parent));rewind(f);char line[4096];assert(fgets(line,sizeof(line),f)&&line[strlen(line)-1]=='\n');
  auto *object=json_tokener_parse(line);assert(object);auto *bytes=json_object_object_get(object,"bytes");
  assert(json_object_get_uint64(bytes)==9007199254740993ULL);json_object *raw=nullptr;assert(json_object_object_get_ex(object,"raw_cycles",&raw)&&raw==nullptr);
  json_object_put(object);fclose(f);
 }else return 2;
 puts("TIMING_CONTRACT_HOST_TEST_PASS_GPU_CYCLES_NULL");return 0;
}
