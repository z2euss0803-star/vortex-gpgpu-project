#include "../../tools/hunet_session.h"
#include "../../tools/hunet_timing.h"
#include <vxl/hunet.h>
#include <cstdarg>
#include <cassert>
#include <cstdio>
#include <time.h>
static vxl_hunet_job_v1 staged{};static unsigned submits,fail_at,reaps;static unsigned fake_clock;static uint64_t now_ns;static bool in_flight,copyout_failure;
extern "C" int __real_clock_gettime(clockid_t,struct timespec*);
extern "C" int __wrap_clock_gettime(clockid_t clock,struct timespec* t){if(!fake_clock)return __real_clock_gettime(clock,t);assert(clock==CLOCK_MONOTONIC);t->tv_sec=now_ns/1000000000;t->tv_nsec=now_ns%1000000000;return 0;}
extern "C" int __wrap_ioctl(int,unsigned long cmd,...){va_list a;va_start(a,cmd);auto*r=va_arg(a,vxl_request*);va_end(a);
 if(cmd==VXL_COPY_TO)memcpy(&staged,reinterpret_cast<void*>(r->user_ptr),128);
 if(cmd==VXL_COPY_FROM)memset(reinterpret_cast<void*>(r->user_ptr),0,size_t(r->length));
 if(cmd==VXL_SUBMIT){assert(staged.sequence==++submits&&r->trust_id==6);if(submits==fail_at){errno=EIO;return -1;}in_flight=true;if(copyout_failure&&submits==3){errno=EFAULT;return -1;}r->job_id=submits;if(fake_clock==2&&submits==6)now_ns+=29000000000ULL;}
 if(cmd==VXL_GET_STATUS){r->job_id=submits;r->state=in_flight?VXL_RUNNING:VXL_COMPLETED;}
 if(cmd==VXL_WAIT){if(fake_clock==2&&submits==6){assert(r->wait_ms<=1000);now_ns+=r->wait_ms*1000000;errno=ETIMEDOUT;return -1;}if(fake_clock)now_ns+=30000000000ULL;r->state=VXL_COMPLETED;r->result=0;in_flight=false;}
 if(cmd==VXL_REAP)++reaps;
 if(cmd==VXL_HUNET_GET_EVIDENCE){auto*e=reinterpret_cast<vxl_hunet_job_evidence_v1*>(r->user_ptr);memset(e,0,256);e->magic=VXL_HUNET_EVIDENCE_MAGIC;e->version=1;e->bytes=256;e->job_id=r->job_id;e->sequence=staged.sequence;e->software_state=in_flight?VXL_RUNNING:VXL_COMPLETED;}
 if(cmd==VXL_HUNET_END_ROI){auto*e=reinterpret_cast<vxl_hunet_end_v1*>(r->user_ptr);memset(e,0,128);e->completed_jobs=submits;}return 0;
}
int main(){
 vxl_hunet_begin_v1 b{};for(bool diag:{false,true}){vxl_hunet_client c{};c.fd=123;c.loaded=1;c.program_id=6;c.handles[0]=1;c.handles[2]=3;c.handles[3]=4;HunetRaw r;submits=0;fail_at=0;assert(!hunet_execute(&c,b,diag,&r));assert(r.jobs.size()==1023&&r.evidence.size()==1023&&r.logits.size()==32420&&r.input.size()==196608&&r.guards.size()==13568&&r.trace.size()==4056&&r.submit_wait_ns.size()==1023);assert(r.diagnostic.size()==(diag?414:0));}
 vxl_hunet_client c{};c.fd=123;c.loaded=1;c.program_id=6;HunetRaw r;submits=0;fail_at=8;assert(hunet_execute(&c,b,false,&r)==-EIO);assert(submits==8&&r.evidence.size()==7&&c.stopped);
 bool reversed=false;try{hunet_duration(10,9);}catch(...){reversed=true;}assert(reversed);puts("HUNET_RAW_SIZES_100_STAGES_314_TRACES_FIRST_ERROR_PROTOCOL_ONLY_PASS");
 for(unsigned which:{1u,2u}){vxl_hunet_client deadline{};deadline.fd=123;deadline.loaded=1;deadline.program_id=6;deadline.handles[0]=1;deadline.handles[2]=3;deadline.handles[3]=4;HunetRaw limited;submits=0;fail_at=0;reaps=0;fake_clock=which;now_ns=1;
  assert(hunet_execute(&deadline,b,false,&limited)==-ETIMEDOUT);assert(submits==6&&deadline.stopped&&deadline.last_job==6);assert(limited.failed_evidence.job_id==6);
  if(which==2){assert(in_flight&&reaps==5&&limited.failed_evidence.software_state==VXL_RUNNING);}else assert(!in_flight);
 }
 puts("WHOLE_GRAPH_180S_NO_NEW_START_AND_INFLIGHT_RETENTION_PASS");
 {vxl_hunet_client recovered{};recovered.fd=123;recovered.loaded=1;recovered.program_id=6;recovered.handles[0]=1;recovered.handles[2]=3;recovered.handles[3]=4;HunetRaw failed;submits=0;fail_at=0;reaps=0;fake_clock=0;copyout_failure=true;
  assert(hunet_execute(&recovered,b,false,&failed)==-EFAULT);assert(submits==3&&reaps==2&&recovered.last_job==3&&recovered.stopped);assert(failed.failed_evidence.job_id==3&&failed.failed_evidence.software_state==VXL_COMPLETED);assert(!in_flight&&failed.jobs.size()==3);
 }
 puts("START_COPYOUT_RECOVER_FINAL_EVIDENCE_STOP_NO_RESUBMIT_PASS");
}
