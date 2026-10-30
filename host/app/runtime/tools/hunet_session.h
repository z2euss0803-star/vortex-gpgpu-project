#pragma once
#include <vxl/hunet_client.h>
#include <vector>
#include <string>
struct HunetRaw {
 std::vector<vxl_hunet_job_v1> jobs;std::vector<vxl_hunet_job_evidence_v1> evidence;
 std::vector<uint8_t> input,logits,guards,trace;
 struct Capture {std::string name;std::vector<uint8_t> bytes;};std::vector<Capture> diagnostic;
 std::vector<uint64_t> submit_wait_ns;
 vxl_hunet_end_v1 end{};vxl_hunet_job_evidence_v1 failed_evidence{};
 uint64_t resident_ns=0,diagnostic_capture_ns=0;int error=0;
};
int hunet_execute(vxl_hunet_client *,const vxl_hunet_begin_v1 &,bool,HunetRaw *);
