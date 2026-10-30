#include <vxunified/joint_bridge.hpp>
#include <vxunified/sealed_assets.hpp>
#include <vxunified/nn_result_metadata.hpp>
#include <vxl/hunet.h>
#include "nnrt/package/json.hpp"
namespace vxunified {
namespace {
Digest hash(const std::string&s){Digest d{};vxl_hunet_hash(s.data(),s.size(),d.data());return d;}
std::string hex(const Digest&d){std::string s;for(auto b:d){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
bool export_leaf(const std::string&path,const char*leaf){
 auto slash=path.find('/');if(slash==std::string::npos||!slash||slash>128||path.substr(slash+1)!=leaf)return false;
 return std::all_of(path.begin(),path.begin()+slash,[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';});
}
}
Result<RegisteredTextureFile>joint_texture_file(const StartupContext&c,const PublishedResult&p,const NativeResultExport&saved,const Digest&expected_package){
 using R=Result<RegisteredTextureFile>;try{
  const auto&h=c.identity;
  if(!s8_joint_platform(h)||c.mode!=BackendMode::COMPUTE_V2||(c.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&c.scope!=vxnn::EvidenceScope::REAL_BOARD)||!p.execution_complete||p.lease.app!=AppKind::NN||p.lease.scope!=c.scope||p.lease.producer.boot_id!=c.boot_id||p.lease.hardware!=h.hardware_contract||!p.binding)return R::failure({EXDEV,"JOINT_BRIDGE","current joint boot and completed original neural result required"});
  auto metadata=nn_result_artifact_layout(p);if(!metadata)return R::failure(*metadata.error);
  const auto&b=*p.binding;const auto&a=p.lease.artifacts[0];
  if(b.app!=AppKind::NN||b.hardware!=h.hardware_contract||b.bit!=h.bit||b.xsa!=h.xsa||b.compatible!=h.compatible||b.provider_flags!=4||b.device_generation!=h.device_generation||b.model!=p.lease.model||b.plan!=p.lease.plan||!nonzero_digest(b.input)||!nonzero_digest(b.source)||!nonzero_digest(a.sha)||a.sha!=saved.tensor.sha||a.bytes!=saved.tensor.bytes||a.shape!=saved.tensor.shape)return R::failure({EXDEV,"JOINT_BRIDGE","original tensor and full execution identity differ"});
  if(!export_leaf(saved.manifest.path,"mosaic-result.json")||!export_leaf(saved.tensor.path,"output.f32")||saved.manifest.path.substr(0,saved.manifest.path.find('/'))!=saved.tensor.path.substr(0,saved.tensor.path.find('/'))||saved.manifest.layout!="JSON_UTF8"||saved.tensor.layout!="CHW_F32"||!nonzero_digest(saved.manifest.sha)||!saved.manifest.bytes||saved.manifest.bytes>262144)return R::failure({EINVAL,"JOINT_BRIDGE","only original exporter files may form the fixed bridge"});
  RegisteredTextureFile spec;spec.id=hex(saved.manifest.sha);spec.result_sha=saved.manifest.sha;spec.result_path=c.private_dir+"/outputs/"+saved.manifest.path;spec.policy.package_directory=c.private_dir+"/texture-inbox/"+spec.id;
  auto original=read_sealed_native_asset(spec.result_path,262144);if(!original)return R::failure(*original.error);
  if(original.value->size()!=saved.manifest.bytes||hash(*original.value)!=saved.manifest.sha)return R::failure({ESTALE,"JOINT_BRIDGE","saved original Mosaic manifest bytes changed"});
  if(p.lease.artifacts.size()>1){namespace j=vxnn::json;auto exported=j::parse(*original.value);auto*o=exported.get();auto*n=j::member(o,"native_origin");if(j::str(j::member(o,"app"))!="mosaic"||j::digest(j::member(n,"model"))!=b.model||j::digest(j::member(n,"plan"))!=b.plan||j::digest(j::member(n,"input"))!=b.input||j::digest(j::member(n,"tensor"))!=a.sha||!nonzero_digest(j::digest(j::member(n,"program")))||!nonzero_digest(j::digest(j::member(n,"implementation")))||j::str(j::member(n,"raw_legacy_json"))!=p.raw_legacy_json||j::digest(j::member(n,"raw_legacy_sha256"))!=hash(p.raw_legacy_json))return R::failure({ESTALE,"JOINT_BRIDGE","exported Mosaic model/input/plan/program/receipt provenance differs"});}
  auto raw=read_sealed_native_asset(spec.policy.package_directory+"/manifest.json",262144);if(!raw)return R::failure(*raw.error);
  namespace j=vxnn::json;auto package=j::parse(*raw.value);auto*source=j::member(package.get(),"source");
  if(j::digest(j::member(source,"result_sha256"))!=saved.manifest.sha||j::digest(j::member(source,"tensor_sha256"))!=saved.tensor.sha||j::str(j::member(source,"scope"))!=(c.scope==vxnn::EvidenceScope::REAL_BOARD?"REAL_BOARD":"OFFLINE"))return R::failure({EXDEV,"JOINT_BRIDGE","derived package belongs to another original result or scope"});
  if(p.lease.artifacts.size()>1&&j::str(j::member(source,"kind"))!="MOSAIC_RESULT")return R::failure({EXDEV,"JOINT_BRIDGE","neural output cannot claim ordinary uploaded-image provenance"});
  if(!nonzero_digest(expected_package)||j::digest(j::member(package.get(),"package_id"))!=expected_package)return R::failure({ESTALE,"JOINT_BRIDGE","package differs from independent trusted B conversion receipt"});
  spec.policy.expected_package_id=expected_package;
  return R::success(std::move(spec));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"JOINT_BRIDGE","bounded bridge metadata allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"JOINT_BRIDGE",e.what()});}
}
}
