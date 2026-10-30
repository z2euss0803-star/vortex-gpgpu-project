#pragma once
#include <vxl/hunet_fused_client.h>
#include "hunet_hybrid_session.h"
#include <array>
enum class FusedMode {STAGE0,DIAGNOSTIC,MEASURED};
struct FusedGpuRecord {
 std::array<vxl_u32,32> args{};
 vxl_fused_meta meta{};
 vxl_hunet_job_evidence_v1 evidence{};
 uint64_t submit_wait_ns=0,evidence_ns=0,reap_ns=0,capture_ns=0;
};
/* JSON-only timing extension: binary GPU/CPU receipt layouts stay unchanged. */
struct FusedCpuDetail {
 uint64_t handler_ns=0,checkpoint_ns=0,input_sha_ns=0,output_sha_ns=0,gelu_call_ns=0;
};
struct FusedRaw {
 std::vector<FusedGpuRecord> gpu;
 std::vector<HybridCpuRecord> cpu;
 std::vector<FusedCpuDetail> cpu_detail;
 std::vector<uint8_t> input,logits;
 vxl_hybrid_end_v1 end{};
 uint64_t load_ns=0,resident_ns=0,checkpoint_ns=0,diagnostic_capture_ns=0,final_read_ns=0;
 uint64_t roi_begin_call_ns=0,roi_end_call_ns=0;
 unsigned logical_completed=0,stage_count=0,captured_stages=0;
 bool prefix_stop=false,measured_authorized=false;
 std::array<vxl_u8,32> numeric_gate_binding{};
 int error=0;
 std::function<void(const std::string&,const void*,size_t)> checkpoint;
 std::function<void(const FusedGpuRecord&)> gpu_checked; /* optional delivery/audit observer; no extra GPU work */
};
static_assert(sizeof(FusedGpuRecord)==448&&sizeof(HybridCpuRecord)==712,"fixed LP64 raw-record schema");
int hunet_fused_execute(vxl_fused_client*,const vxl_hunet_begin_v1&,FusedMode,FusedRaw*);
