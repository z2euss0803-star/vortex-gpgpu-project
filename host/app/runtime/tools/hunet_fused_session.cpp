#include "hunet_fused_session.h"
#include "hunet_hybrid_reference.h"
#include "hunet_timing.h"
#include <vxl/hunet.h>
#include "../generated/hunet/stages.h"
#include "../generated/hunet/assets.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
static void need(int rc){if(rc)throw rc;}
static void check(bool b){if(!b)throw -EIO;}
static std::string name(const char*g,unsigned i,const char*s){char b[100];snprintf(b,sizeof(b),"%s/%03u%s",g,i,s);return b;}
static void save(FusedRaw*r,const std::string&n,const void*p,size_t bytes){uint64_t b=hunet_now();r->checkpoint(n,p,bytes);r->checkpoint_ns+=hunet_duration(b,hunet_now());}
static void finite(const std::vector<uint8_t>&bytes){check(bytes.size()%4==0);for(size_t i=0;i<bytes.size();i+=4){float f;memcpy(&f,bytes.data()+i,4);if(!std::isfinite(f))throw -EDOM;}}
static int capture(vxl_fused_client*c,uint64_t a,uint64_t n,std::vector<uint8_t>&out){out.resize(n);return vxl_hunet_client_copy(&c->gpu,c->gpu.handles[2],a-VXL_HUNET_WS,out.data(),n,0);}
static void stage(vxl_fused_client*c,unsigned id,FusedRaw*r){
 std::vector<uint8_t>b;uint64_t begin=hunet_now(),prior=r->checkpoint_ns;
 need(capture(c,vxl_hunet_stages[id].gpu,vxl_hunet_stages[id].bytes,b));finite(b);
 save(r,name("stages",id,".f32"),b.data(),b.size());r->captured_stages++;
 r->diagnostic_capture_ns+=hunet_duration(begin,hunet_now())-(r->checkpoint_ns-prior);
}
static void validate(const FusedGpuRecord&g,const vxl_hunet_begin_v1&b,unsigned gen,uint64_t id){
 const auto&e=g.evidence;const auto&m=g.meta;vxl_u8 sha[32];vxl_hunet_hash(g.args.data(),128,sha);
 check(e.magic==VXL_HUNET_EVIDENCE_MAGIC&&e.version==1&&e.bytes==256&&!e.flags&&!memcmp(e.run_id,b.run_id,16)&&e.generation==gen&&e.job_id==id&&e.sequence==m.first&&e.stage_id==m.stage&&e.op==m.op&&e.program_id==9);
 check(e.software_state==VXL_COMPLETED&&!e.result&&!e.errors&&!e.pending_r&&!e.pending_w&&e.ready==1&&e.idle==1&&(e.status&2)&&!(e.status&4)&&e.jobs_after==vxl_u32(e.jobs_before+1)&&e.watchdog==4000000000u&&e.crt_guard_flags==3&&(e.valid_flags&27)==27&&!(e.valid_flags&~31u)&&!(e.cdma_status&0x4770));
 if(m.op==15){check(e.valid_flags&VXL_HUNET_VALID_OWNER);unsigned active=(m.trace_elements+3)/4;if(active>4)active=4;for(unsigned c=0;c<4;c++)check(e.owner_counts[c]==(c<active?m.trace_elements/active+(c<m.trace_elements%active):0));}
 check(!vxl_hunet_payload_validate(VXL_HUNET_GET_EVIDENCE,&e,256)&&!memcmp(sha,e.args_sha256,32));for(unsigned i=0;i<4;i++)check(!e.exitcode[i]);
 check(e.submit_ns<=e.done_ns&&e.done_ns<=e.ack_ns&&e.execution_deadline_ns==e.submit_ns+60000000000ULL);
}
static void cpu_body(vxl_fused_client*c,const vxl_hunet_job_v1&j,const vxl_hunet_begin_v1&b,bool diag,FusedRaw*r){
 auto&detail=r->cpu_detail.back();auto&record=r->cpu.back();auto&p=record.begin;auto&q=record.commit;auto&e=record.evidence;
 size_t n=j.tasks;std::vector<float>x(n),y(n),ref(diag?n:0);std::vector<uint8_t>writeback;vxl_u8 sha[32];
 p.magic=VXL_HYBRID_BEGIN_MAGIC;p.version=1;p.bytes=128;memcpy(p.run_id,b.run_id,16);p.generation=c->binding.v2.v1.generation;p.sequence=j.sequence;p.stage=j.stage_id;p.dtype=1;p.input_ptr=(uintptr_t)x.data();p.input_bytes=p.output_bytes=n*4;
 uint64_t start=hunet_now();need(vxl_fused_client_begin_cpu(c,&p));record.begin_call_ns=hunet_duration(start,hunet_now());check(p.token!=0);
 if(diag){save(r,name("cpu",j.stage_id,"-begin.bin"),&p,128);save(r,name("cpu",j.stage_id,"-input.f32"),x.data(),n*4);}
 start=hunet_now();vxl_hunet_hash(x.data(),n*4,sha);detail.input_sha_ns=hunet_duration(start,hunet_now());check(!memcmp(sha,p.input_sha256,32));
 start=hunet_now();int gelu_rc=hunet_cpu_gelu4(x.data(),y.data(),n,&record.timing);detail.gelu_call_ns=hunet_duration(start,hunet_now());need(gelu_rc);check(detail.gelu_call_ns>=record.timing.duration_ns);
 if(diag){save(r,name("cpu",j.stage_id,"-output.f32"),y.data(),n*4);start=hunet_now();need(hunet_hybrid_reference1(x.data(),ref.data(),n));record.reference_ns=hunet_duration(start,hunet_now());save(r,name("cpu",j.stage_id,"-reference.f32"),ref.data(),n*4);check(!memcmp(y.data(),ref.data(),n*4));}
 q.magic=VXL_HYBRID_COMMIT_MAGIC;q.version=1;q.bytes=128;memcpy(q.run_id,b.run_id,16);q.generation=p.generation;q.sequence=j.sequence;q.stage=j.stage_id;q.algorithm=1;q.threads=4;q.token=p.token;q.output_ptr=(uintptr_t)y.data();q.output_bytes=n*4;q.cpu_duration_ns=record.timing.duration_ns;start=hunet_now();vxl_hunet_hash(y.data(),n*4,q.output_sha256);detail.output_sha_ns=hunet_duration(start,hunet_now());
 start=hunet_now();need(vxl_fused_client_commit_cpu(c,&q));record.commit_call_ns=hunet_duration(start,hunet_now());start=hunet_now();need(vxl_fused_client_evidence(c,p.token,&e));record.evidence_call_ns=hunet_duration(start,hunet_now());
 check(!vxl_hybrid_payload_validate(VXL_HYBRID_GET_CPU_EVIDENCE,&e,384)&&e.kind==1&&e.token==p.token&&e.generation==p.generation&&e.sequence==j.sequence&&e.stage==p.stage&&!memcmp(e.run_id,b.run_id,16)&&e.algorithm==1&&e.threads==4&&!e.result&&e.state==VXL_HYBRID_CPU_COMMITTED&&e.check_flags==63&&e.input_bytes==n*4&&e.output_bytes==n*4&&e.user_cpu_ns==record.timing.duration_ns&&e.gpu_jobs_before==e.gpu_jobs_after);
 check(!memcmp(e.input_sha256,p.input_sha256,32)&&!memcmp(e.output_sha256,q.output_sha256,32)&&!memcmp(e.catalog_sha256,vxl_hunet_jobs_sha,32));
 if(diag){start=hunet_now();need(capture(c,j.y,n*4,writeback));record.writeback_capture_ns=hunet_duration(start,hunet_now());save(r,name("cpu",j.stage_id,"-writeback.f32"),writeback.data(),writeback.size());check(!memcmp(writeback.data(),y.data(),n*4));}
}
static void cpu(vxl_fused_client*c,const vxl_hunet_job_v1&j,const vxl_hunet_begin_v1&b,bool diag,FusedRaw*r){
 uint64_t begin=hunet_now(),prior=r->checkpoint_ns;
 r->cpu.emplace_back();r->cpu_detail.emplace_back();
 auto finish=[&](){r->cpu_detail.back().handler_ns=hunet_duration(begin,hunet_now());r->cpu_detail.back().checkpoint_ns=r->checkpoint_ns-prior;};
 try{cpu_body(c,j,b,diag,r);}catch(...){finish();throw;}
 /* Includes vector destruction and all validation/copy/hash outside workers. */
 finish();
}
static void flush(FusedRaw*r){
 save(r,"gpu-records.bin",r->gpu.data(),r->gpu.size()*sizeof(FusedGpuRecord));
 save(r,"cpu-records.bin",r->cpu.data(),r->cpu.size()*sizeof(HybridCpuRecord));
}
int hunet_fused_execute(vxl_fused_client*c,const vxl_hunet_begin_v1&b,FusedMode mode,FusedRaw*r){
 if(!c||!r||!r->checkpoint)return -EINVAL;
 bool diag=mode!=FusedMode::MEASURED;uint64_t begin=hunet_now(),deadline=begin+uint64_t(VXL_HUNET_NETWORK_MS)*1000000ULL;unsigned seq=1;
 auto budget=[&](){uint64_t now=hunet_now();if(now>=deadline||deadline<begin)throw -ETIMEDOUT;uint64_t ms=(deadline-now)/1000000;if(!ms)throw -ETIMEDOUT;return ms>60000?60000:ms;};
 try{
  if(!diag){vxl_u8 sha[32];auto binding=c->binding;binding.v2.v1.generation=binding.v2.v1.state=0;vxl_hunet_hash(&binding,512,sha);if(!r->measured_authorized||memcmp(sha,r->numeric_gate_binding.data(),32))throw -EPERM;}
  save(r,"begin.bin",&b,128);uint64_t roi_start=hunet_now();int roi_rc=vxl_hunet_client_begin(&c->gpu,&b);r->roi_begin_call_ns=hunet_duration(roi_start,hunet_now());need(roi_rc);
  while(seq<=1023){
   (void)budget();const vxl_u32*logical=vxl_hunet_catalog_job(seq);check(logical!=nullptr);unsigned last=seq,stage_id=logical[30];
   if(logical[3]==6){vxl_hunet_job_v1 j;memcpy(&j,logical,128);cpu(c,j,b,diag,r);}
   else{
    const vxl_u32*args=vxl_fused_catalog_job(seq);check(args!=nullptr);r->gpu.emplace_back();auto&g=r->gpu.back();memcpy(g.args.data(),args,128);need(vxl_fused_decode_meta(args,128,&g.meta));last=g.meta.last;stage_id=g.meta.stage;
    uint64_t id=0,start=hunet_now();need(vxl_hunet_client_submit(&c->gpu,reinterpret_cast<const vxl_hunet_job_v1*>(args),&id));need(vxl_hunet_client_wait(&c->gpu,id,budget()));g.submit_wait_ns=hunet_duration(start,hunet_now());
    start=hunet_now();need(vxl_hunet_client_evidence(&c->gpu,id,&g.evidence));g.evidence_ns=hunet_duration(start,hunet_now());validate(g,b,c->binding.v2.v1.generation,id);
    if(r->gpu_checked)r->gpu_checked(g);
    start=hunet_now();need(vxl_hunet_client_reap(&c->gpu,id));g.reap_ns=hunet_duration(start,hunet_now());
   }
   r->logical_completed=last;
   if(last==vxl_hunet_stages[stage_id].last_sequence){r->stage_count++;if(diag)stage(c,stage_id,r);}
   if(mode==FusedMode::STAGE0){check(seq==1&&last==96&&r->gpu.size()==1);r->prefix_stop=true;flush(r);r->resident_ns=hunet_duration(begin,hunet_now());return 0;}
   seq=last+1;
  }
  uint64_t start=hunet_now();need(capture(c,VXL_HUNET_INPUT,196608,r->input));need(capture(c,VXL_HUNET_LOGITS,32420,r->logits));r->final_read_ns=hunet_duration(start,hunet_now());finite(r->logits);
  save(r,"input.f32",r->input.data(),r->input.size());save(r,"logits.f32",r->logits.data(),r->logits.size());(void)budget();roi_start=hunet_now();roi_rc=vxl_fused_client_end(c,&r->end);r->roi_end_call_ns=hunet_duration(roi_start,hunet_now());need(roi_rc);save(r,"end.bin",&r->end,256);
  check(r->end.logical_jobs==1023&&r->end.gpu_jobs==103&&r->end.cpu_jobs==13&&r->end.stages==100&&r->end.final_idle==1&&r->end.check_flags==31&&r->end.hw_jobs_after-r->end.hw_jobs_before==103);
  vxl_u8 sha[32];vxl_hunet_hash(r->input.data(),r->input.size(),sha);check(!memcmp(sha,b.input_sha256,32)&&!memcmp(sha,r->end.input_sha256,32));vxl_hunet_hash(r->logits.data(),r->logits.size(),sha);check(!memcmp(sha,r->end.logits_sha256,32));flush(r);r->resident_ns=hunet_duration(begin,hunet_now());return 0;
 }catch(int error){r->error=error;}catch(const std::exception&){r->error=-EIO;}
 c->gpu.stopped=1;r->resident_ns=hunet_duration(begin,hunet_now());
 try{
  std::string s="{\"state\":\"FAIL_STOP\",\"error\":"+std::to_string(r->error)+",\"sequence\":"+std::to_string(seq)+",\"validated_logical_jobs\":"+std::to_string(r->logical_completed)+"}\n";save(r,"failure.json",s.data(),s.size());flush(r);
  if(c->gpu.last_job){vxl_hunet_job_evidence_v1 e{};if(!vxl_hunet_client_evidence(&c->gpu,c->gpu.last_job,&e))save(r,"failed-gpu-evidence.bin",&e,256);}
  if(!r->cpu.empty()&&r->cpu.back().begin.token){vxl_hybrid_cpu_evidence_v1 e{};if(!vxl_fused_client_evidence(c,r->cpu.back().begin.token,&e))save(r,"failed-cpu-evidence.bin",&e,384);}
 }catch(...){/* Already saved records remain immutable. */}
 return r->error;
}
