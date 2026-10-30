#include <vxunified/startup.hpp>
#include <vxunified/installation.hpp>
#include "nnrt/package/json.hpp"
#include <vxl/hunet.h>
namespace vxunified {
Result<DaemonConfig>decode_daemon_config(const std::string&raw,const std::string&boot){
 try{
  namespace json=vxnn::json;
  json::check(!raw.empty()&&raw.size()<=65536,"startup config limit64KiB");auto tree=json::parse(raw);auto*o=tree.get();json::fields(o,{"version","private_dir","evidence_dir","device_path","backend_mode","legacy_profile","hardware_contract","bit","xsa","compatible","device_generation","provider_flags","runtime_integrity"});json::check(json::u32(json::member(o,"version"))==1,"startup version1 required");
  DaemonConfig c;c.boot_id=boot;
  json::check(!boot.empty()&&boot.size()<=128&&std::all_of(boot.begin(),boot.end(),[](unsigned char x){return (x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='-'||x=='_';}),"kernel boot identity required");
  auto path=[&](const char*k){auto s=json::str(json::member(o,k));json::check(s.size()>1&&s.size()<=4096&&s[0]=='/'&&s.find('\0')==std::string::npos,"trusted absolute startup path required");return s;};
  c.service.private_dir=path("private_dir");c.service.device_path=path("device_path");c.evidence_dir=path("evidence_dir");json::check(c.service.private_dir.size()+14<108,"private socket path limit");
  auto mode=json::str(json::member(o,"backend_mode"));json::check(mode=="compute-v2"||mode=="legacy-compat","explicit compute-v2 or legacy-compat, never auto");c.service.mode=mode=="compute-v2"?BackendMode::COMPUTE_V2:BackendMode::LEGACY_COMPAT;c.legacy_profile=json::u32(json::member(o,"legacy_profile"));
  json_object*integrity=nullptr;if(json_object_object_get_ex(o,"runtime_integrity",&integrity)){auto policy=json::str(integrity);json::check(policy=="strict"||policy=="fast","explicit strict or fast integrity policy required");c.fast_runtime=policy=="fast";}
  json::check(!c.fast_runtime||c.service.mode==BackendMode::COMPUTE_V2,"FAST requires compute-v2 capability negotiation");
  c.identity.hardware_contract=json::digest(json::member(o,"hardware_contract"));c.identity.bit=json::digest(json::member(o,"bit"));c.identity.xsa=json::digest(json::member(o,"xsa"));c.identity.compatible=json::digest(json::member(o,"compatible"));c.identity.device_generation=json::u64(json::member(o,"device_generation"));c.identity.provider_flags=json::u32(json::member(o,"provider_flags"));
  json::check(c.identity.device_generation&&nonzero_digest(c.identity.hardware_contract)&&nonzero_digest(c.identity.bit)&&nonzero_digest(c.identity.xsa)&&nonzero_digest(c.identity.compatible)&&(c.identity.provider_flags==1||c.identity.provider_flags==2||s8_joint_platform(c.identity)),"complete startup identity required");
  json::check(c.service.mode==BackendMode::COMPUTE_V2?!c.legacy_profile:((c.legacy_profile==4||c.legacy_profile==6)&&c.identity.provider_flags==1),"fixed legacy NN profile or v2 profile0 required");
  // Configuration is a declaration, never a user-selectable verification tier.
  c.identity.board_identity_verified=false;c.scope=vxnn::EvidenceScope::REAL_BOARD;vxl_hunet_hash(raw.data(),raw.size(),c.config_sha.data());return Result<DaemonConfig>::success(std::move(c));
 }catch(const std::bad_alloc&){return Result<DaemonConfig>::failure({ENOMEM,"STARTUP_CONFIG","startup allocation"});}catch(const std::exception&e){return Result<DaemonConfig>::failure({EINVAL,"STARTUP_CONFIG",e.what()});}
}
// No empty-ready or synthetic fallback. These files are sealed by the matched
// image build; RPC and model packages cannot replace their trust declaration.
ApplicationBuilder installed_applications(){return installed_applications_from("/etc/vxgpud/installation.json","/etc/vxgpud/installation.sha256");}
}
