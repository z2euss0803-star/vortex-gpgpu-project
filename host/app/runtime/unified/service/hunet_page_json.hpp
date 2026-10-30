#pragma once
#include "nnrt/package/json.hpp"
#include <cmath>
namespace vxunified { namespace hunet_page_detail {
// Page-result indices contain finite raw FP32 scores. Validate structure with
// the unchanged integer-only canonical checker after masking only valid float
// lexemes; parse original bytes afterwards. No package admission is relaxed.
inline vxnn::json::Ptr parse_index(const std::string&raw){
 namespace j=vxnn::json;
 j::check(!raw.empty()&&raw.size()<=(256u<<10),"bounded final page index required");
 std::string masked=raw;
 for(size_t at=0;at<raw.size();){
  if(raw[at]=='"'){
   ++at;while(at<raw.size()){char c=raw[at++];if(c=='\\'&&at<raw.size()){++at;}else if(c=='"'){break;}}
   continue;
  }
  if(raw[at]!='-'&&(raw[at]<'0'||raw[at]>'9')){++at;continue;}
  const auto first=at;if(raw[at]=='-'){++at;}
  j::check(at<raw.size()&&raw[at]>='0'&&raw[at]<='9',"invalid page numeric token");
  if(raw[at]=='0'){++at;}else{while(at<raw.size()&&raw[at]>='0'&&raw[at]<='9'){++at;}}
  bool floating=false;
  if(at<raw.size()&&raw[at]=='.'){
   floating=true;++at;const auto digits=at;while(at<raw.size()&&raw[at]>='0'&&raw[at]<='9'){++at;}
   j::check(at>digits,"page fraction requires digits");
  }
  if(at<raw.size()&&(raw[at]=='e'||raw[at]=='E')){
   floating=true;++at;if(at<raw.size()&&(raw[at]=='+'||raw[at]=='-')){++at;}
   const auto digits=at;while(at<raw.size()&&raw[at]>='0'&&raw[at]<='9'){++at;}
   j::check(at>digits,"page exponent requires digits");
  }
  if(floating){masked[first]='0';for(size_t i=first+1;i<at;i++){masked[i]=' ';}}
 }
 auto valid=vxnn::canonical_metadata(masked);j::check(bool(valid),valid.error?valid.error->detail:"invalid page JSON");
 auto*t=json_tokener_new_ex(32);j::check(t!=nullptr,"page JSON allocation");
 json_tokener_set_flags(t,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);
 const auto input=raw+" ";auto*value=json_tokener_parse_ex(t,input.c_str(),int(input.size()));
 const bool good=json_tokener_get_error(t)==json_tokener_success;json_tokener_free(t);
 j::Ptr result(value,json_object_put);j::check(good&&value,"strict final page JSON required");
 std::vector<json_object*>pending{value};
 while(!pending.empty()){
  auto*node=pending.back();pending.pop_back();if(!node){continue;}
  if(json_object_is_type(node,json_type_double)){j::check(std::isfinite(json_object_get_double(node)),"nonfinite page numeric token");}
  else if(json_object_is_type(node,json_type_array)){for(size_t i=0;i<json_object_array_length(node);i++){pending.push_back(json_object_array_get_idx(node,i));}}
  else if(json_object_is_type(node,json_type_object)){json_object_object_foreach(node,key,child){(void)key;pending.push_back(child);}}
 }
 return result;
}
}}
