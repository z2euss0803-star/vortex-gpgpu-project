#pragma once
#include <vxl/hunet_hybrid_client.h>
#include <vxl/hunet_cpu_gelu.h>
#include <vector>
#include <functional>
#include <string>
enum class HybridMode {FIRST_GELU,DIAGNOSTIC,FORMAL};
struct HybridGpuRecord {vxl_hunet_job_v1 job{};vxl_hunet_job_evidence_v1 evidence{};uint64_t submit_wait_ns=0,evidence_ns=0,reap_ns=0,capture_ns=0;};
struct HybridCpuRecord {vxl_hybrid_begin_v1 begin{};vxl_hybrid_commit_v1 commit{};vxl_hybrid_cpu_evidence_v1 evidence{};hunet_cpu_timing timing{};uint64_t begin_call_ns=0,commit_call_ns=0,evidence_call_ns=0,writeback_capture_ns=0,reference_ns=0;};
struct HybridRaw {
 std::vector<vxl_hunet_job_v1> logical;
 std::vector<HybridGpuRecord> gpu;
 std::vector<HybridCpuRecord> cpu;
 std::vector<uint8_t> input,logits,guards,trace;
 vxl_hybrid_end_v1 end{};
 uint64_t resident_ns=0,checkpoint_ns=0,diagnostic_capture_ns=0;
 unsigned logical_completed=0;
 bool prefix_stop=false,formal_authorized=false;
 int error=0;
 std::function<void(const std::string &,const void *,size_t)> checkpoint;
};
int hunet_hybrid_execute(vxl_hybrid_client *,const vxl_hunet_begin_v1 &,HybridMode,HybridRaw *);
