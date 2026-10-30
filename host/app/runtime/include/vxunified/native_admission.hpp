#pragma once
#include <vxunified/daemon.hpp>
namespace vxunified {
inline bool native_identity_equal(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;}
// Authorized 2026-10-24: checked common roots admit native I/O without
// promoting the CAPS/manifest marker to independent bitstream measurement.
inline Result<void>admit_native_startup(const StartupContext&c,uint32_t provider){
 auto rt=c.runtime.lock();
 if(provider!=1&&provider!=2&&provider!=4)return Result<void>::failure({EOPNOTSUPP,"NATIVE_ADMISSION","explicit frozen provider required"});
 if(!rt||c.mode!=BackendMode::COMPUTE_V2||!c.compute_backend||c.borrowed_fd<0||!rt->owns_native_backend(c.borrowed_fd,c.compute_backend.get())||c.results.lock()!=rt->trusted_results())return Result<void>::failure({EXDEV,"NATIVE_ADMISSION","same live native owner/backend/results required"});
 const auto a=rt->identity();const auto&b=c.identity;
 if(a.hardware_contract!=b.hardware_contract||a.bit!=b.bit||a.xsa!=b.xsa||a.compatible!=b.compatible||a.device_generation!=b.device_generation||a.provider_flags!=b.provider_flags||a.board_identity_verified!=b.board_identity_verified||(!s8_joint_platform(b)&&b.provider_flags!=provider)||(b.provider_flags==4&&!s8_joint_platform(b))||!b.device_generation||!nonzero_digest(b.hardware_contract)||!nonzero_digest(b.bit)||!nonzero_digest(b.xsa)||!nonzero_digest(b.compatible))return Result<void>::failure({EXDEV,"NATIVE_ADMISSION","full common hardware identity/epoch/measurement tier differs"});
 if(c.scope!=rt->evidence_scope()||(c.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&c.scope!=vxnn::EvidenceScope::REAL_BOARD)||c.boot_id.empty()||c.boot_id.size()>128||c.private_dir.empty()||c.private_dir[0]!='/'||(c.scope==vxnn::EvidenceScope::REAL_BOARD&&c.journal.expired()))return Result<void>::failure({EXDEV,"NATIVE_ADMISSION","exact scope/boot/private root and native evidence journal required"});
 return Result<void>::success();
}
}
