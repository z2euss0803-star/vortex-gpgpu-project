#include "hunet_hybrid_session.h"
#include "hunet_hybrid_reference.h"
#include "hunet_timing.h"
#include <vxl/hunet.h>
#include "../generated/hunet/layout.h"
#include "../generated/hunet/stages.h"
#include <cstdio>
#include <cmath>
#include <stdexcept>
static void need(int rc){if(rc)throw rc;}
static void check(bool b){if(!b)throw -EIO;}
static std::string name(const char *group,unsigned index,const char *suffix){char b[100];snprintf(b,sizeof(b),"%s/%03u-%s",group,index,suffix);return b;}
static void save(HybridRaw *r,const std::string &n,const void *p,size_t bytes){uint64_t b=hunet_now();r->checkpoint(n,p,bytes);r->checkpoint_ns+=hunet_duration(b,hunet_now());}
static int capture(vxl_hybrid_client *c,uint64_t gpu,uint64_t bytes,std::vector<uint8_t> &out){out.resize(bytes);return vxl_hunet_client_copy(&c->gpu,c->gpu.handles[2],gpu-VXL_HUNET_WS,out.data(),bytes,0);}
static void finite(const std::vector<uint8_t> &b){check(b.size()%4==0);for(size_t i=0;i<b.size();i+=4){float f;memcpy(&f,b.data()+i,4);if(!std::isfinite(f))throw -EDOM;}}
static void trace_valid(const std::vector<uint8_t> &bytes,unsigned tiles){
 check(tiles&&bytes.size()==tiles*4);unsigned active=(tiles+3)/4;if(active>4)active=4;size_t off=0;
 for(unsigned core=0;core<active;core++)for(unsigned n=0;n<tiles/active+(core<tiles%active);n++){vxl_u32 v;memcpy(&v,bytes.data()+off,4);off+=4;check(v==core+1);}
}
static void gpu_valid(const vxl_hunet_job_evidence_v1 &e,const vxl_hunet_job_v1 &j,const vxl_hunet_begin_v1 &b,unsigned gen,uint64_t id){
 check(e.magic==VXL_HUNET_EVIDENCE_MAGIC&&e.version==1&&e.bytes==256&&e.flags==0&&!memcmp(e.run_id,b.run_id,16)&&e.generation==gen&&e.job_id==id&&e.sequence==j.sequence&&e.op==j.op&&e.stage_id==j.stage_id&&e.program_id==8);
 check(e.software_state==VXL_COMPLETED&&e.result==0&&e.errors==0&&e.pending_r==0&&e.pending_w==0&&e.ready==1&&e.idle==1&&e.jobs_after==vxl_u32(e.jobs_before+1)&&e.watchdog==4000000000u&&(e.status&2)&&!(e.status&4)&&(e.valid_flags&27)==27&&!(e.valid_flags&~31u)&&!(e.cdma_status&0x4770)&&e.crt_guard_flags==3);
 check(!vxl_hunet_payload_validate(VXL_HUNET_GET_EVIDENCE,&e,256));
 for(unsigned i=0;i<4;i++)check(!e.exitcode[i]);
 vxl_u8 sha[32];vxl_hunet_hash(&j,128,sha);check(!memcmp(sha,e.args_sha256,32));check(e.submit_ns<=e.done_ns&&e.done_ns<=e.ack_ns&&e.execution_deadline_ns==e.submit_ns+60000000000ULL);
}
int hunet_hybrid_execute(vxl_hybrid_client *c,const vxl_hunet_begin_v1 &b,HybridMode mode,HybridRaw *r){
 if(!c||!r||!r->checkpoint)return -EINVAL;
 const bool diag=mode!=HybridMode::FORMAL;uint64_t begin=hunet_now(),deadline=begin+uint64_t(VXL_HUNET_NETWORK_MS)*1000000ULL,last_token=0;unsigned current_seq=0;
 auto budget=[&](){uint64_t now=hunet_now();if(now>=deadline)throw -ETIMEDOUT;uint64_t ms=(deadline-now)/1000000ULL;if(!ms)throw -ETIMEDOUT;return ms>60000?60000:ms;};
 try{
  if(mode==HybridMode::FORMAL&&!r->formal_authorized)throw -EPERM;
  if(deadline<begin)throw -EOVERFLOW;
  save(r,"begin.bin",&b,128);need(vxl_hunet_client_begin(&c->gpu,&b));
  const unsigned last=mode==HybridMode::FIRST_GELU?392:1023;
  for(unsigned seq=1;seq<=last;seq++){
   current_seq=seq;(void)budget();vxl_hunet_job_v1 j{};memcpy(&j,vxl_hunet_catalog_job(seq),128);r->logical.push_back(j);
   save(r,name("logical",seq,"args.bin"),&j,128);
   if(j.op!=6){
    HybridGpuRecord g;g.job=j;uint64_t id=0,start=hunet_now();need(vxl_hunet_client_submit(&c->gpu,&j,&id));need(vxl_hunet_client_wait(&c->gpu,id,budget()));g.submit_wait_ns=hunet_duration(start,hunet_now());
    start=hunet_now();need(vxl_hunet_client_evidence(&c->gpu,id,&g.evidence));g.evidence_ns=hunet_duration(start,hunet_now());save(r,name("gpu",seq,"evidence.bin"),&g.evidence,256);gpu_valid(g.evidence,j,b,c->binding.v1.generation,id);
    if(diag){
     start=hunet_now();uint64_t saved_checkpoint=r->checkpoint_ns;std::vector<uint8_t> bytes;char label[100];
     if(mode==HybridMode::FIRST_GELU&&j.stage_id==9&&j.op==1){
      need(capture(c,j.y,uint64_t(j.tasks)*2,bytes));snprintf(label,sizeof(label),"packs/%04u.f16",seq);save(r,label,bytes.data(),bytes.size());
      for(size_t off=0;off<bytes.size();off+=2){uint16_t h;memcpy(&h,bytes.data()+off,2);check((h&0x7c00)!=0x7c00);}
     }
     if(j.op==2){need(capture(c,j.scratch,uint64_t(j.tasks)*4,bytes));snprintf(label,sizeof(label),"traces/%04u.u32",seq);save(r,label,bytes.data(),bytes.size());trace_valid(bytes,j.tasks);}
     if(seq==vxl_hunet_stages[j.stage_id].last_sequence){need(capture(c,vxl_hunet_stages[j.stage_id].gpu,vxl_hunet_stages[j.stage_id].bytes,bytes));snprintf(label,sizeof(label),"stages/%03u.f32",j.stage_id);save(r,label,bytes.data(),bytes.size());finite(bytes);}
     g.capture_ns=hunet_duration(start,hunet_now())-(r->checkpoint_ns-saved_checkpoint);r->diagnostic_capture_ns+=g.capture_ns;
    }
    start=hunet_now();need(vxl_hunet_client_reap(&c->gpu,id));g.reap_ns=hunet_duration(start,hunet_now());r->gpu.push_back(g);
    std::string metrics="{\"role\":\"GPU\",\"sequence\":"+std::to_string(seq)+",\"T_submit_wait_ns\":"+std::to_string(g.submit_wait_ns)+",\"T_evidence_ns\":"+std::to_string(g.evidence_ns)+",\"T_reap_ns\":"+std::to_string(g.reap_ns)+",\"T_capture_ns\":"+std::to_string(g.capture_ns)+"}\n";save(r,name("metrics",seq,"timing.json"),metrics.data(),metrics.size());
   }else{
    HybridCpuRecord cpu;size_t n=j.tasks;std::vector<float> x(n),y(n),ref(diag?n:0);std::vector<uint8_t> writeback;auto &p=cpu.begin;
    p.magic=VXL_HYBRID_BEGIN_MAGIC;p.version=1;p.bytes=128;memcpy(p.run_id,b.run_id,16);p.generation=c->binding.v1.generation;p.sequence=seq;p.stage=j.stage_id;p.dtype=1;p.input_ptr=reinterpret_cast<uintptr_t>(x.data());p.input_bytes=p.output_bytes=n*4;
    uint64_t start=hunet_now();need(vxl_hybrid_client_begin_cpu(c,&p));cpu.begin_call_ns=hunet_duration(start,hunet_now());last_token=p.token;check(p.token!=0);save(r,name("cpu",j.stage_id,"begin.bin"),&p,128);save(r,name("cpu",j.stage_id,"input.f32"),x.data(),n*4);
    vxl_u8 sha[32];vxl_hunet_hash(x.data(),n*4,sha);check(!memcmp(sha,p.input_sha256,32));need(hunet_cpu_gelu4(x.data(),y.data(),n,&cpu.timing));save(r,name("cpu",j.stage_id,"output.f32"),y.data(),n*4);save(r,name("cpu",j.stage_id,"compute.bin"),&cpu.timing,sizeof(cpu.timing));
    if(diag){start=hunet_now();need(hunet_hybrid_reference1(x.data(),ref.data(),n));cpu.reference_ns=hunet_duration(start,hunet_now());save(r,name("cpu",j.stage_id,"reference.f32"),ref.data(),n*4);check(!memcmp(y.data(),ref.data(),n*4));}
    auto &q=cpu.commit;q.magic=VXL_HYBRID_COMMIT_MAGIC;q.version=1;q.bytes=128;memcpy(q.run_id,b.run_id,16);q.generation=p.generation;q.sequence=seq;q.stage=p.stage;q.algorithm=1;q.token=p.token;q.output_ptr=reinterpret_cast<uintptr_t>(y.data());q.output_bytes=n*4;q.cpu_duration_ns=cpu.timing.duration_ns;q.threads=4;vxl_hunet_hash(y.data(),n*4,q.output_sha256);save(r,name("cpu",j.stage_id,"commit.bin"),&q,128);
    start=hunet_now();need(vxl_hybrid_client_commit_cpu(c,&q));cpu.commit_call_ns=hunet_duration(start,hunet_now());start=hunet_now();need(vxl_hybrid_client_evidence(c,p.token,&cpu.evidence));cpu.evidence_call_ns=hunet_duration(start,hunet_now());save(r,name("cpu",j.stage_id,"evidence.bin"),&cpu.evidence,384);
    const auto &e=cpu.evidence;check(e.magic==VXL_HYBRID_EVIDENCE_MAGIC&&e.version==1&&e.bytes==384&&e.flags==0&&e.kind==1&&e.token==p.token&&e.generation==p.generation&&e.sequence==seq&&e.stage==p.stage&&!memcmp(e.run_id,b.run_id,16)&&e.algorithm==1&&e.threads==4&&e.result==0&&e.state==VXL_HYBRID_CPU_COMMITTED&&e.check_flags==63&&e.input_bytes==n*4&&e.output_bytes==n*4&&e.user_cpu_ns==cpu.timing.duration_ns&&e.gpu_jobs_before==e.gpu_jobs_after);
    check(!memcmp(e.input_sha256,p.input_sha256,32)&&!memcmp(e.output_sha256,q.output_sha256,32)&&!memcmp(e.catalog_sha256,c->binding.v1.jobs_sha256,32));
    start=hunet_now();need(capture(c,j.y,n*4,writeback));cpu.writeback_capture_ns=hunet_duration(start,hunet_now());save(r,name("cpu",j.stage_id,"writeback.f32"),writeback.data(),writeback.size());check(!memcmp(writeback.data(),y.data(),n*4));
    if(diag){char label[80];snprintf(label,sizeof(label),"stages/%03u.f32",j.stage_id);save(r,label,writeback.data(),writeback.size());}
    check(!vxl_hybrid_payload_validate(VXL_HYBRID_GET_CPU_EVIDENCE,&e,384));r->cpu.push_back(cpu);
    std::string metrics="{\"role\":\"CPU\",\"sequence\":"+std::to_string(seq)+",\"stage\":"+std::to_string(j.stage_id)+",\"T_begin_call_ns\":"+std::to_string(cpu.begin_call_ns)+",\"T_compute_ns\":"+std::to_string(cpu.timing.duration_ns)+",\"T_commit_call_ns\":"+std::to_string(cpu.commit_call_ns)+",\"T_evidence_call_ns\":"+std::to_string(cpu.evidence_call_ns)+",\"T_writeback_capture_ns\":"+std::to_string(cpu.writeback_capture_ns)+",\"T_cpu_reference_ns\":"+std::to_string(cpu.reference_ns)+"}\n";save(r,name("metrics",seq,"timing.json"),metrics.data(),metrics.size());
   }
   r->logical_completed++;
  }
  if(mode==HybridMode::FIRST_GELU){need(capture(c,VXL_HUNET_INPUT,196608,r->input));save(r,"input.f32",r->input.data(),r->input.size());r->prefix_stop=true;std::string s="{\"state\":\"PREFIX_STOP\",\"logical_jobs\":392,\"gpu_jobs\":391,\"cpu_jobs\":1}\n";save(r,"prefix-stop.json",s.data(),s.size());r->resident_ns=hunet_duration(begin,hunet_now());return 0;}
  need(capture(c,VXL_HUNET_INPUT,196608,r->input));save(r,"input.f32",r->input.data(),r->input.size());need(capture(c,VXL_HUNET_LOGITS,32420,r->logits));save(r,"logits.f32",r->logits.data(),r->logits.size());finite(r->logits);need(capture(c,0x87044080,4056,r->trace));save(r,"last_trace.u32",r->trace.data(),r->trace.size());trace_valid(r->trace,1014);
  for(unsigned i=0;i<106;i++)for(unsigned side=0;side<2;side++){std::vector<uint8_t> g;need(capture(c,side?vxl_hunet_slots[i].start+vxl_hunet_slots[i].bytes:vxl_hunet_slots[i].start-64,64,g));for(auto v:g)check(v==0xa5);r->guards.insert(r->guards.end(),g.begin(),g.end());}
  save(r,"guards.bin",r->guards.data(),r->guards.size());
  (void)budget();need(vxl_hybrid_client_end(c,&r->end));save(r,"end.bin",&r->end,256);check(r->end.logical_jobs==1023&&r->end.gpu_jobs==1010&&r->end.cpu_jobs==13&&r->end.stages==100&&r->end.final_idle==1&&r->end.check_flags==31);
  vxl_u8 sha[32];vxl_hunet_hash(r->input.data(),r->input.size(),sha);check(!memcmp(sha,b.input_sha256,32)&&!memcmp(sha,r->end.input_sha256,32));vxl_hunet_hash(r->logits.data(),r->logits.size(),sha);check(!memcmp(sha,r->end.logits_sha256,32));r->resident_ns=hunet_duration(begin,hunet_now());return 0;
 }catch(int error){r->error=error;}catch(const std::exception &){r->error=-EIO;}
 c->gpu.stopped=1;r->resident_ns=hunet_duration(begin,hunet_now());
 try{
  std::string s="{\"state\":\"FAIL_STOP\",\"error\":"+std::to_string(r->error)+",\"sequence\":"+std::to_string(current_seq)+",\"validated_logical_jobs\":"+std::to_string(r->logical_completed)+"}\n";save(r,"failure.json",s.data(),s.size());
  if(last_token){vxl_hybrid_cpu_evidence_v1 e{};if(!vxl_hybrid_client_evidence(c,last_token,&e))save(r,"failed-cpu-evidence.bin",&e,384);}
  if(c->gpu.last_job){vxl_hunet_job_evidence_v1 e{};if(!vxl_hunet_client_evidence(&c->gpu,c->gpu.last_job,&e))save(r,"failed-gpu-evidence.bin",&e,256);}
 }catch(...){/* Earlier checkpoints remain; caller reports storage failure. */}
 return r->error;
}
