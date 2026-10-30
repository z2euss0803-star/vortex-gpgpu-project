#pragma once
#include <vxnn/types.hpp>
#include <uapi/vx_compute_v2.h>
#include <algorithm>
#include <cerrno>
namespace vxunified {
using vxnn::Digest;using vxnn::Result;using vxnn::DeviceCaps;using vxnn::BufferRef;
using vxnn::ProgramRef;using vxnn::JobDesc;using vxnn::Completion;using vxnn::Event;using vxnn::RequestKey;
enum class AppKind:uint32_t {NN=1,P8=2,IMAGE=3};
enum class BackendMode {LEGACY_COMPAT,COMPUTE_V2};
enum class Consumer {CPU,DMA,GPU,TEX,DP};
struct BackendIdentity {Digest hardware_contract{},bit{},xsa{},compatible{};uint64_t device_generation=0;uint32_t provider_flags=0;bool board_identity_verified=false;};
struct ProgramKey {uint32_t namespace_id=0,logical_profile=0,logical_program=0;Digest code{},schema{},hardware_contract{};};
enum class PixelFormat:uint32_t {RGBA8888=1,XRGB8888=2};
struct DisplayLayout {uint32_t canvas_width=0,canvas_height=0,source_width=0,source_height=0;uint32_t viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;PixelFormat format=PixelFormat::RGBA8888;};
struct DisplayReceipt {uint64_t cookie=0;int result=0;vxnn::ClockDomain clock_domain=vxnn::ClockDomain::LINUX_MONOTONIC;bool presented=false,had_old_front=false,old_retired=false;std::optional<uint64_t> submitted_ns,presented_ns,old_retired_ns;};
inline bool nonzero_digest(const Digest& d){return std::any_of(d.begin(),d.end(),[](uint8_t v){return v!=0;});}
inline bool digest_matches_hex(const Digest&d,const char*hex){
 const char*digits="0123456789abcdef";
 for(size_t i=0;i<d.size();++i)if(digits[d[i]>>4]!=hex[2*i]||digits[d[i]&15]!=hex[2*i+1])return false;
 return true;
}
inline bool s8_joint_platform(const BackendIdentity&b){
 return b.provider_flags==4&&b.device_generation&&
  digest_matches_hex(b.hardware_contract,"1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d")&&
  digest_matches_hex(b.bit,"94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1")&&
  digest_matches_hex(b.xsa,"02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94")&&
  digest_matches_hex(b.compatible,"f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");
}
inline bool s8_provider(const BackendIdentity&b){return b.provider_flags==1||s8_joint_platform(b);}
inline bool p8_provider(const BackendIdentity&b){return b.provider_flags==2||s8_joint_platform(b);}
inline Result<void> admit_program(const BackendIdentity& b,const ProgramKey& p){
 if(!b.device_generation || !nonzero_digest(b.hardware_contract) || !nonzero_digest(b.bit) || !nonzero_digest(b.xsa) || !nonzero_digest(b.compatible))return Result<void>::failure({EINVAL,"identity","unbound device"});
 if(b.hardware_contract!=p.hardware_contract)return Result<void>::failure({EXDEV,"identity","hardware contract mismatch"});
 if(!nonzero_digest(p.code) || !nonzero_digest(p.schema) || !p.logical_profile || !p.logical_program)return Result<void>::failure({EINVAL,"identity","incomplete program key"});
 const bool joint=s8_joint_platform(b)&&((p.namespace_id==1&&p.logical_profile==1&&(p.logical_program==65552||p.logical_program==65568))||(p.namespace_id==2&&p.logical_profile==5&&p.logical_program>=9&&p.logical_program<=12)||(p.namespace_id==3&&p.logical_profile==256&&p.logical_program>=512&&p.logical_program<=521));
 const bool allowed=(b.provider_flags==1 && (p.namespace_id==1 || p.namespace_id==3)) || (b.provider_flags==2 && p.namespace_id==2)||joint;
 if(!allowed)return Result<void>::failure({EOPNOTSUPP,"identity","namespace not supported by frozen provider"});
 return Result<void>::success();
}
inline vxl_v2_caps query_caps(const BackendIdentity& b,const DeviceCaps& c){
 vxl_v2_caps out{};out.version=2;out.bytes=256;out.device_generation=b.device_generation;
 std::copy(b.hardware_contract.begin(),b.hardware_contract.end(),out.hardware_contract);
 std::copy(b.bit.begin(),b.bit.end(),out.bit);std::copy(b.xsa.begin(),b.xsa.end(),out.xsa);std::copy(b.compatible.begin(),b.compatible.end(),out.compatible);
 out.ps_base=c.ps_base;out.ps_bytes=c.ps_bytes;out.pl_base=c.pl_base;out.pl_bytes=c.pl_bytes;
 out.max_objects=c.max_objects;out.max_pending=c.max_pending;out.max_completions=c.max_completions;out.max_refs=c.max_refs_per_job;
 out.max_args=c.max_args_bytes;out.max_programs=c.max_programs;out.cycle_bits=c.cycle_counter_bits;out.contexts=1;
 out.feature_mask=b.provider_flags;out.identity_source=VXL_V2_ID_BOOT_MANIFEST_DECLARED;return out;
}
}
