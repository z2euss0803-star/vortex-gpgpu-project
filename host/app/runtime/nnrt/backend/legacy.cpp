#include <vxnn/backend.hpp>
#include <vxnn/evidence.hpp>
#include <vxl/demo_mosaic.h>
#include <vxl/demo_hunet.h>
#include <vxl/hunet.h>
#include <json-c/json.h>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <time.h>
#include <unistd.h>
namespace vxnn {
namespace {
using Json=std::unique_ptr<json_object,decltype(&json_object_put)>;
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000u+uint64_t(t.tv_nsec);}
std::string text(json_object *object,const char *key){json_object *v=nullptr;return object&&json_object_object_get_ex(object,key,&v)&&json_object_is_type(v,json_type_string)?json_object_get_string(v):"";}
Result<LegacyResult> error(int rc,const char *category,const std::string &detail){return Result<LegacyResult>::failure({rc,category,detail});}
}
Result<LegacyResult> LegacyBackend::run(const LegacyInput &input,EvidenceSink &sink,demo_app app){
 if(first_error_)return error(first_error_,"LEGACY_FIRST_ERROR","successor dispatch blocked");
 if(session_.quarantined)return error(-EIO,"DEVICE_UNAVAILABLE","session remains quarantined");
 if(input.borrowed_fd<0||input.borrowed_fd!=session_.owner_fd||input.release_root.string()!=release_root_.string())return error(-EBADF,"LEGACY_ADMISSION","borrowed owner/release mismatch");
 if(input.raw_request.empty()||input.raw_request.size()>DEMO_MAX_JSON||!input.input.data||!input.input.size||input.input.size>DEMO_MAX_ASSET||input.shape.empty())return error(-EINVAL,"LEGACY_ADMISSION","missing frozen request/input");
 size_t bytes=4;for(uint64_t dim:input.shape){if(!dim||dim>std::numeric_limits<size_t>::max()/bytes)return error(-EOVERFLOW,"LEGACY_ADMISSION","shape overflow");bytes*=size_t(dim);}
 if(bytes!=input.input.size)return error(-EMSGSIZE,"LEGACY_ADMISSION","FP32 input shape length mismatch");
 auto request=std::make_unique<demo_run>();int rc=demo_parse_run(input.raw_request.data(),input.raw_request.size(),request.get());
 if(rc)return error(rc,"LEGACY_ADMISSION","old request codec refused");
 if(request->app!=app||request->generation!=session_.generation||request->run_id!=input.run_id||bool(request->diagnostics)!=input.diagnostics)return error(-ESTALE,"LEGACY_ADMISSION","request identity mismatch");
 Json original(json_tokener_parse(input.raw_request.c_str()),json_object_put);
 json_object *identities=nullptr,*shape=nullptr;json_object_object_get_ex(original.get(),"identities",&identities);json_object_object_get_ex(original.get(),"shape",&shape);
 if(!shape||json_object_array_length(shape)!=input.shape.size())return error(-ESTALE,"LEGACY_ADMISSION","shape profile mismatch");
 for(size_t i=0;i<input.shape.size();++i)if(json_object_get_uint64(json_object_array_get_idx(shape,i))!=input.shape[i])return error(-ESTALE,"LEGACY_ADMISSION","shape profile mismatch");
 const bool real=input.scope==EvidenceScope::REAL_BOARD;
 if(text(original.get(),"scope")!=(real?"REAL_BOARD":"OFFLINE"))return error(-EPERM,"LEGACY_ADMISSION","evidence scope mismatch");
 Digest digest{};vxl_hunet_hash(input.input.data,input.input.size,digest.data());
 char hash[65]={};const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;++i){hash[2*i]=hex[digest[i]>>4];hash[2*i+1]=hex[digest[i]&15];}
 if(digest!=input.input_sha||text(identities,"input")!=hash)return error(-ESTALE,"LEGACY_ADMISSION","input digest mismatch");
 if(next_event_>std::numeric_limits<uint64_t>::max()-2)return error(-EOVERFLOW,"EVIDENCE","event generation exhausted");
 Event event;char host[256]={};if(gethostname(host,sizeof(host)-1))return error(-errno,"EVIDENCE","host identity unavailable");
 event.host=host;event.run_id=input.run_id;event.request_id=input.run_id;event.boot_id=text(original.get(),"boot_id");event.source_id=text(identities,"source");event.model_id=input.model_id.empty()?text(identities,"model"):input.model_id;event.plan_id=input.plan_id;
 event.operator_id=app==DEMO_HUNET?"LEGACY_PROGRAM9":"LEGACY_PROGRAM12";event.clock_domain=real?ClockDomain::LINUX_MONOTONIC:ClockDomain::HOST_MONOTONIC;event.scope=input.scope;event.device_generation=session_.generation;event.bytes=input.input.size;
 const uint64_t start=now();if(!start)return error(-ENODATA,"EVIDENCE","monotonic clock unavailable");
 event.event_id=next_event_++;event.start_ns=start;event.end_ns=start;
 auto begin=sink.emit(event);if(!begin){first_error_=begin.error->code;return error(first_error_,"EVIDENCE",begin.error->detail);}
 auto output=std::make_unique<demo_result>();
 rc=app==DEMO_HUNET?demo_hunet_run(&session_,request.get(),output.get()):demo_mosaic_run(&session_,request.get(),output.get());
 event.event_id=next_event_++;event.end_ns=now();event.status=rc;
 if(!rc&&real&&output->run.cycle_valid){event.cycles_valid=true;event.raw_cycles=output->run.raw_cycles;}
 auto emitted=sink.emit(event);
 if(rc){first_error_=rc;return error(rc,"LEGACY_EXECUTION","original backend error retained; no successor"+(!emitted?"; secondary_evidence_error="+std::to_string(emitted.error->code):""));}
 if(!emitted){
  first_error_=emitted.error->code;
  const size_t retained=output->run.json_bytes>DEMO_MAX_JSON?DEMO_MAX_JSON:output->run.json_bytes;
  return error(first_error_,"EVIDENCE",emitted.error->detail+"; original_result_bytes="+std::to_string(output->run.json_bytes)+"; ORIGINAL_LEGACY_RESULT="+std::string(output->run.json,retained));
 }
 if(output->run.json_bytes>DEMO_MAX_JSON){first_error_=-EPROTO;return error(first_error_,"LEGACY_RESULT","old result length invalid");}
 LegacyResult result;result.raw_json.assign(output->run.json,output->run.json_bytes);
 Json parsed(json_tokener_parse(result.raw_json.c_str()),json_object_put);
 if(!parsed){first_error_=-EBADMSG;return error(first_error_,"LEGACY_RESULT","invalid ORIGINAL_LEGACY_RESULT="+result.raw_json);}
 const auto numeric=text(parsed.get(),"numeric_class");
 if(numeric=="PASS")result.numeric_class=NumericClass::PASS;
 else if(numeric=="KNOWN_NUMERIC_DEVIATION")result.numeric_class=NumericClass::KNOWN_NUMERIC_DEVIATION;
 else if(numeric=="NEW_MISMATCH"||numeric=="FAIL")result.numeric_class=NumericClass::NEW_MISMATCH;
 json_object *assets=nullptr;if(json_object_object_get_ex(parsed.get(),"artifacts",&assets)&&json_object_is_type(assets,json_type_array))for(size_t i=0;i<json_object_array_length(assets);++i)result.raw_artifacts.emplace_back(json_object_to_json_string_ext(json_object_array_get_idx(assets,i),JSON_C_TO_STRING_PLAIN));
 return Result<LegacyResult>::success(std::move(result));
}
Result<LegacyResult> LegacyBackend::run_hunet(const LegacyInput &i,EvidenceSink &s){return run(i,s,DEMO_HUNET);}
Result<LegacyResult> LegacyBackend::run_mosaic(const LegacyInput &i,EvidenceSink &s){return run(i,s,DEMO_MOSAIC);}
}
