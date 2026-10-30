#include <vxnn/backend.hpp>
#include <vxnn/evidence.hpp>
#include <vxl/hunet.h>
#include <json-c/json.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unistd.h>
using namespace vxnn;
static unsigned calls,opens,closes;static int injected;static std::string old_json;
extern "C" int __wrap_open(const char *,int,...){++opens;return -1;}
extern "C" int __wrap_close(int){++closes;return -1;}
static int old_run(demo_session *s,const demo_run *r,demo_result *out){
 assert(s->owner_fd==77&&r->generation==1);++calls;if(injected)return injected;
 *out={};out->run=*r;out->run.json_bytes=old_json.size();memcpy(out->run.json,old_json.data(),old_json.size());return 0;
}
extern "C" int demo_mosaic_run(demo_session *s,const demo_run *r,demo_result *out){return old_run(s,r,out);}
extern "C" int demo_hunet_run(demo_session *s,const demo_run *r,demo_result *out){return old_run(s,r,out);}
struct Sink:EvidenceSink {std::vector<Event> events;bool fail=false;size_t fail_after=0;Result<void> emit(const Event &e)override{if(fail||(fail_after&&events.size()+1>=fail_after))return Result<void>::failure({-ENOSPC,"EVIDENCE","full"});events.push_back(e);return Result<void>::success();}};
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1]);assert(f);std::string raw{std::istreambuf_iterator<char>(f),{}};
 auto *o=json_tokener_parse(raw.c_str());assert(o);auto *identities=json_object_object_get(o,"identities");
 json_object_object_add(o,"state",json_object_new_string("STAGED"));
 const uint8_t data[1*3*8*8*4]={1,2,3,4};LegacyInput input;input.input={data,sizeof(data)};vxl_hunet_hash(data,sizeof(data),input.input_sha.data());
 char hash[65]={};const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;++i){hash[2*i]=hex[input.input_sha[i]>>4];hash[2*i+1]=hex[input.input_sha[i]&15];}
 json_object_object_add(identities,"input",json_object_new_string(hash));input.raw_request=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);json_object_put(o);
 input.borrowed_fd=77;input.release_root="/frozen/release";input.shape={1,3,8,8};input.run_id="11111111111111111111111111111111";
 old_json="{\"numeric_class\":\"KNOWN_NUMERIC_DEVIATION\",\"counter\":9007199254740993,\"artifacts\":[]}";
 demo_session session{};session.owner_fd=77;session.generation=1;LegacyBackend backend(session,"/frozen/release");Sink sink;
 auto invalid=input;invalid.borrowed_fd=88;auto wrong_owner=backend.run_mosaic(invalid,sink);assert(!wrong_owner&&wrong_owner.error->code==-EBADF&&!calls);
 invalid=input;invalid.input_sha[0]^=1;auto wrong_input=backend.run_mosaic(invalid,sink);assert(!wrong_input&&wrong_input.error->code==-ESTALE&&!calls);
 auto result=backend.run_mosaic(input,sink);assert(result&&result.value->raw_json==old_json);
 assert(result.value->numeric_class==NumericClass::KNOWN_NUMERIC_DEVIATION);assert(calls==1&&!opens&&!closes);
 for(const auto &event:sink.events)assert(!event.cycles_valid&&!event.raw_cycles.has_value());
 injected=-EIO;auto failure=backend.run_mosaic(input,sink);assert(!failure&&failure.error->code==-EIO);unsigned before=calls;
 auto successor=backend.run_mosaic(input,sink);assert(!successor&&successor.error->code==-EIO&&calls==before);
 demo_session other{};other.owner_fd=77;other.generation=1;LegacyBackend no_log(other,"/frozen/release");Sink bad_sink;bad_sink.fail=true;injected=0;
 auto no_receipt=no_log.run_mosaic(input,bad_sink);assert(!no_receipt&&no_receipt.error->code==-ENOSPC&&calls==before);
 LegacyBackend late_error(other,"/frozen/release");Sink late_sink;late_sink.fail_after=2;
 auto late=late_error.run_mosaic(input,late_sink);assert(!late&&late.error->code==-ENOSPC&&late.error->detail.find(old_json)!=std::string::npos);
 before=calls;assert(!late_error.run_mosaic(input,late_sink)&&calls==before);
 assert(!opens&&!closes);puts("LEGACY_BORROWED_FD_BYTES_FIRST_ERROR_EVIDENCE_HOST_PROTOCOL_PASS_GPU_CYCLES_NULL");return 0;
}
