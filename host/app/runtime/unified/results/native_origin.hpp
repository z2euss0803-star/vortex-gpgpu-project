#pragma once
#include "io.hpp"
namespace vxunified {namespace result_io {
struct NativeFileOrigin {RequestKey producer;Digest plan{};vxnn::EvidenceScope scope=vxnn::EvidenceScope::OFFLINE;};
inline std::optional<NativeFileOrigin>native_origin(io_json::Ptr meta){
 namespace j=io_json;auto*o=meta.get();if(!j::has(o,"native_origin"))return std::nullopt;
 auto*n=j::member(o,"native_origin");j::fields(n,{"format","evidence_scope","boot_id","client_id","request_id","device_generation","provider_flags","input_bytes","hardware","bit","xsa","compatible","model","plan","input","source","program","implementation","tensor","raw_legacy_json","raw_legacy_sha256","quant_binding_format","layout_binding_format","numeric_class","build_id"});
 j::check(j::str(j::member(n,"format"))=="unified-native-mosaic-origin-v1","native origin format");
 auto sum=[](const std::string&s){Digest d{};vxl_hunet_hash(s.data(),s.size(),d.data());return d;};
 auto digest=[&](const char*k){auto d=j::digest(j::member(n,k));j::check(std::any_of(d.begin(),d.end(),[](uint8_t b){return b!=0;}),"native nonzero identity");return d;};
 for(const auto*k:{"hardware","bit","xsa","compatible","model","plan","input","source","program","implementation","tensor","raw_legacy_sha256"})(void)digest(k);
 NativeFileOrigin result;result.producer={j::str(j::member(n,"boot_id")),j::str(j::member(n,"client_id")),j::str(j::member(n,"request_id"))};
 for(const auto*s:{&result.producer.boot_id,&result.producer.client_id,&result.producer.request_id})j::check(!s->empty()&&s->size()<=128&&std::all_of(s->begin(),s->end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';}),"native producer identity");
 j::check(j::u32(j::member(n,"device_generation"))==j::u32(j::member(o,"generation"))&&j::u32(j::member(n,"device_generation"))&&(j::u32(j::member(n,"provider_flags"))==1||j::u32(j::member(n,"provider_flags"))==4),"native original epoch/provider");
 if(j::u32(j::member(n,"provider_flags"))==4){BackendIdentity hw;hw.hardware_contract=digest("hardware");hw.bit=digest("bit");hw.xsa=digest("xsa");hw.compatible=digest("compatible");hw.provider_flags=4;hw.device_generation=j::u32(j::member(n,"device_generation"));j::check(s8_joint_platform(hw),"exact versioned S8 joint original source identity required");}
 const auto input_bytes=j::u64(j::member(n,"input_bytes"));j::check(input_bytes&&input_bytes<=(64u<<20),"native input extent");
 for(const auto*k:{"boot_id","build_id","numeric_class"})j::check(j::str(j::member(n,k))==j::str(j::member(o,k)),"native envelope text differs");
 auto scope=j::str(j::member(n,"evidence_scope"));j::check(scope=="OFFLINE"||scope=="HOST_PROTOCOL_TEST"||scope=="REAL_BOARD","native original evidence scope");
 j::check(j::str(j::member(o,"scope"))==(scope=="REAL_BOARD"?"REAL_BOARD":"OFFLINE"),"native envelope scope projection");
 if(scope=="REAL_BOARD")j::check(j::boolean(j::member(o,"cycle_valid"))&&j::u64(j::member(o,"raw_cycles")),"original GPU cycles required");
 else j::check(!j::boolean(j::member(o,"cycle_valid"))&&!j::member(o,"raw_cycles"),"HOST source cannot claim GPU cycles");
 result.scope=scope=="REAL_BOARD"?vxnn::EvidenceScope::REAL_BOARD:scope=="HOST_PROTOCOL_TEST"?vxnn::EvidenceScope::HOST_PROTOCOL_TEST:vxnn::EvidenceScope::OFFLINE;
 result.plan=digest("plan");auto*ids=j::member(o,"identities");for(const auto*k:{"hardware","source","program","model","input"})j::check(digest(k)==j::digest(j::member(ids,k)),"native envelope identity differs");
 const auto request=result.producer.boot_id+'\0'+result.producer.client_id+'\0'+result.producer.request_id;
 auto hex=[](Digest d){std::string s;for(auto b:d){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;};
 j::check(hex(sum(request)).substr(0,32)==j::str(j::member(o,"run_id")),"native request/result identity differs");
 vxl_hunet_sha h;Digest quant{};vxl_hunet_sha_init(&h);const char domain[]="NATIVE_MOSAIC_QUANT_BINDING_V1";vxl_hunet_sha_update(&h,domain,sizeof(domain));auto implementation=digest("implementation"),model=digest("model");vxl_hunet_sha_update(&h,implementation.data(),32);vxl_hunet_sha_update(&h,model.data(),32);vxl_hunet_sha_final(&h,quant.data());
 j::check(j::str(j::member(n,"quant_binding_format"))==domain&&quant==j::digest(j::member(ids,"quant")),"native quantization implementation/model binding differs");
 auto*shape=j::member(o,"shape");auto layout="NATIVE_CHW_F32_LE_LAYOUT_V1:"+std::to_string(j::u32(j::at(shape,2)))+":"+std::to_string(j::u32(j::at(shape,3)));
 j::check(j::str(j::member(n,"layout_binding_format"))=="NATIVE_CHW_F32_LE_LAYOUT_V1"&&sum(layout)==j::digest(j::member(ids,"layout")),"native layout binding differs");
 auto*rows=j::member(o,"artifacts");unsigned outputs=0;for(size_t i=0;i<j::count(rows,32);++i){auto*row=j::at(rows,i);if(j::str(j::member(row,"role"))=="output"){++outputs;j::check(digest("tensor")==j::digest(j::member(row,"sha256")),"native original tensor differs");}}j::check(outputs==1,"one native output");
 auto raw=j::str(j::member(n,"raw_legacy_json"));j::check(!raw.empty()&&raw.size()<=(256u<<10)&&sum(raw)==digest("raw_legacy_sha256"),"native original receipt differs");return result;
}
}}
