#pragma once
#include <vxnn/backend.hpp>
#include <array>
#include <vxnn/cpu.hpp>
namespace vxnn {
enum class HunetGeluPhase {NONE,VALIDATE,DOWNLOAD,COMPUTE,UPLOAD,READBACK,VERIFY,COMMITTED};
struct HunetGeluTask {NodeId node=0;BufferRef input,output;size_t elements=0;};
struct HunetGeluReceipt {
 uint64_t begin_ns=0,end_ns=0,total_ns=0,download_ns=0,cpu_call_ns=0,upload_ns=0,readback_ns=0,hash_ns=0;
 CpuTiming cpu;Digest input_sha{},output_sha{},readback_sha{};
 HunetGeluPhase phase=HunetGeluPhase::NONE;int error=0;
 bool downloaded=false,computed=false,uploaded=false,verified=false,committed=false;
 bool content_checks_omitted=false;
 std::optional<uint64_t> gpu_raw_cycles; // CPU work has no GPU counter.
};
// Called by the trusted non-GPU port only after predecessor GPU ACK/retirement.
// Strict mode verifies output readback; FAST mode explicitly records omission.
// Both require the original compute/upload completion. No fresh token,
// fd, per-request pool, program loading or kernel/model rule is created here.
Result<void> execute_hunet_gelu(const HunetGeluTask&,IGpuBackend&,CpuPool&,
 std::shared_ptr<CpuCancel>,uint64_t borrowed_token,HunetGeluReceipt& fresh_receipt);
// Same transfer/retirement fields as GELU; statistics expose two committed
// FP32 bit patterns separately. Arithmetic is the original ordered Kahan.
struct HunetStatsTask {NodeId node=0;BufferRef input,output;size_t elements=0;uint32_t epsilon_bits=0;};
struct HunetStatsReceipt:HunetGeluReceipt {std::array<uint32_t,2>value_bits{};};
Result<void> execute_hunet_stats(const HunetStatsTask&,IGpuBackend&,CpuPool&,
 std::shared_ptr<CpuCancel>,uint64_t borrowed_token,HunetStatsReceipt& fresh_receipt);

}
