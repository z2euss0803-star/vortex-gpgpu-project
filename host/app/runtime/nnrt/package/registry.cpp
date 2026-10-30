#include <vxnn/package.hpp>
#include <tuple>
#include "json.hpp"
namespace vxnn {
bool OperatorKey::operator<(const OperatorKey&b)const{return std::tie(name,semantic_version,implementation_sha)<std::tie(b.name,b.semantic_version,b.implementation_sha);}
Result<void> OperatorRegistry::add(const OperatorSpec&s){
 if(s.key.name.empty()||s.key.name.size()>128||s.key.semantic_version.empty()||s.key.semantic_version.size()>64||std::all_of(s.key.implementation_sha.begin(),s.key.implementation_sha.end(),[](uint8_t b){return !b;})||!s.available||s.supported_types.empty()||s.supported_layouts.empty()||!s.infer_shapes||!s.workspace||!s.validate||!s.prepare||!s.execute||!entries_.emplace(s.key,s).second)return Result<void>::failure({EINVAL,"REGISTRY_REJECTED","incomplete or duplicate operator"});
 return Result<void>::success();
}
Result<OperatorSpec> OperatorRegistry::resolve(const std::string&n,const std::string&v,const Digest&d)const{auto it=entries_.find({n,v,d});if(it==entries_.end())return Result<OperatorSpec>::failure({ENOENT,"REGISTRY_REJECTED","exact implementation not registered"});return Result<OperatorSpec>::success(it->second);}
Result<void> OperatorRegistry::validate(const OperatorKey&key,const std::vector<TensorDesc>&in,const ScalarParams&params,const DeviceCaps&caps)const{
 auto s=resolve(key.name,key.semantic_version,key.implementation_sha);if(!s)return Result<void>::failure(*s.error);
 for(auto&t:in)if(std::find(s.value->supported_types.begin(),s.value->supported_types.end(),t.dtype)==s.value->supported_types.end()||std::find(s.value->supported_layouts.begin(),s.value->supported_layouts.end(),t.layout)==s.value->supported_layouts.end())return Result<void>::failure({ENOTSUP,"REGISTRY_REJECTED","dtype/layout unsupported"});
 try{return s.value->validate(in,params,caps);}catch(const std::exception&e){return Result<void>::failure({EINVAL,"REGISTRY_REJECTED",e.what()});}
}
}
