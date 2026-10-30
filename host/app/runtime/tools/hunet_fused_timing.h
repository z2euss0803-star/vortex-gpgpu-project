#pragma once
#include "hunet_fused_session.h"
inline std::string hunet_fused_timings(const FusedRaw&r){
 std::string s="{\"gpu\":[";bool first=true;
 for(const auto&g:r.gpu){if(!first)s+=",";first=false;s+="{\"first\":"+std::to_string(g.meta.first)+",\"last\":"+std::to_string(g.meta.last)+",\"stage\":"+std::to_string(g.meta.stage)+",\"op\":"+std::to_string(g.meta.op)+",\"elapsed_raw\":"+std::to_string(g.evidence.elapsed_raw)+",\"T_submit_wait_ns\":"+std::to_string(g.submit_wait_ns)+",\"T_evidence_ns\":"+std::to_string(g.evidence_ns)+",\"T_reap_ns\":"+std::to_string(g.reap_ns)+"}";}
 s+="],\"cpu\":[";first=true;
 for(const auto&c:r.cpu){if(!first)s+=",";first=false;s+="{\"sequence\":"+std::to_string(c.begin.sequence)+",\"stage\":"+std::to_string(c.begin.stage)+",\"T_begin_call_ns\":"+std::to_string(c.begin_call_ns)+",\"T_compute_ns\":"+std::to_string(c.timing.duration_ns)+",\"T_commit_call_ns\":"+std::to_string(c.commit_call_ns)+",\"T_evidence_call_ns\":"+std::to_string(c.evidence_call_ns)+",\"T_writeback_capture_ns\":"+std::to_string(c.writeback_capture_ns)+",\"T_cpu_reference_ns\":"+std::to_string(c.reference_ns)+"}";}
 s+="],\"handoff_v1\":{\"version\":1,\"T_roi_begin_call_ns\":"+std::to_string(r.roi_begin_call_ns)+",\"T_roi_end_call_ns\":"+std::to_string(r.roi_end_call_ns)+",\"cpu\":[";first=true;
 for(size_t i=0;i<r.cpu_detail.size();i++){const auto&d=r.cpu_detail[i];const auto&c=r.cpu[i];if(!first)s+=",";first=false;
  s+="{\"sequence\":"+std::to_string(c.begin.sequence)+",\"stage\":"+std::to_string(c.begin.stage)+",\"T_handler_ns\":"+std::to_string(d.handler_ns)+",\"T_checkpoint_ns\":"+std::to_string(d.checkpoint_ns)+",\"T_input_sha_ns\":"+std::to_string(d.input_sha_ns)+",\"T_output_sha_ns\":"+std::to_string(d.output_sha_ns)+",\"T_gelu_call_ns\":"+std::to_string(d.gelu_call_ns)+",\"T_gelu_exterior_ns\":"+std::to_string(d.gelu_call_ns>=c.timing.duration_ns?d.gelu_call_ns-c.timing.duration_ns:0)+"}";
 }
 return s+"]}}";
}
