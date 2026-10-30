#pragma once
#include <vxnn/request_input.hpp>
#include <set>
namespace vxunified {
// Pure bounded metadata identity; no file/device/Runtime I/O. Metadata has no
// tensor content. Actual bytes are admitted later by bind_request_input.
inline Result<Digest> hunet_page_input_id(const std::vector<vxnn::RequestInput>&words){
 using R=Result<Digest>;
 try{
  if(words.empty()||words.size()>128)return R::failure({EINVAL,"HUNET_PAGE","word count1..128 required"});
  std::set<std::string>ids;std::string raw="VX_HUNET_PAGE_INPUT_V1:"+std::to_string(words.size())+":";
  auto text=[&](const std::string&s){raw+=std::to_string(s.size())+":"+s;};
  for(size_t ordinal=0;ordinal<words.size();++ordinal){const auto&w=words[ordinal];
   if(w.content||w.id.empty()||w.id.size()>128||!ids.insert(w.id).second||w.dtype!=vxnn::DType::FP32||w.layout!="CHW_F32"||w.shape!=vxnn::Shape{3,128,128}||!nonzero_digest(w.source_sha)||!nonzero_digest(w.tensor_sha)||w.preprocessing.empty()||w.preprocessing.size()>128||w.preprocessing.find('\0')!=std::string::npos||w.preprocessing_id!=vxnn::content_digest(w.preprocessing)||(w.source_format!="PNG"&&w.source_format!="JPEG"&&w.source_format!="BMP"))return R::failure({EINVAL,"HUNET_PAGE","immutable identified FP32 CHW128 metadata required"});
   for(unsigned char c:w.id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return R::failure({EINVAL,"HUNET_PAGE","bounded input identifier required"});
   raw+=std::to_string(ordinal)+":";text(w.id);text(w.source_format);text(w.preprocessing);text(vxnn::digest_hex(w.source_sha));text(vxnn::digest_hex(w.tensor_sha));text(vxnn::digest_hex(w.preprocessing_id));text(w.layout);text("FP32");for(auto d:w.shape)raw+=std::to_string(d)+":";
  }
  return R::success(vxnn::content_digest(raw));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"HUNET_PAGE","metadata identity allocation"});}
}
inline Result<Digest> hunet_page_plan_id(const Digest&base_plan,const std::vector<vxnn::RequestInput>&words){
 if(!nonzero_digest(base_plan))return Result<Digest>::failure({EINVAL,"HUNET_PAGE","base plan identity required"});
 auto input=hunet_page_input_id(words);if(!input)return input;
 return Result<Digest>::success(vxnn::content_digest("VX_HUNET_PAGE_PLAN_V1:"+vxnn::digest_hex(base_plan)+":"+vxnn::digest_hex(*input.value)));
}
}
