#include <vxl/hunet_client.h>
#include <vxl/hunet.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
static unsigned submits,evidences,reaps,fail_at,statuses;static bool output_fault;
extern "C" int __wrap_ioctl(int,unsigned long cmd,...){va_list a;va_start(a,cmd);auto*r=va_arg(a,vxl_request*);va_end(a);
 if(cmd==VXL_SUBMIT){assert(r->trust_id==6&&r->deadline_ms==60000&&r->length==128);++submits;if(output_fault){errno=EFAULT;return -1;}if(submits==fail_at){errno=EIO;return -1;}r->job_id=submits;}
 if(cmd==VXL_GET_STATUS){++statuses;r->job_id=2048;r->state=VXL_RUNNING;r->result=0;}
 if(cmd==VXL_WAIT){r->state=VXL_COMPLETED;r->result=0;}
 if(cmd==VXL_HUNET_GET_EVIDENCE){auto*e=reinterpret_cast<vxl_hunet_job_evidence_v1*>(r->user_ptr);memset(e,0,256);e->job_id=r->job_id;e->software_state=VXL_COMPLETED;evidences++;}
 if(cmd==VXL_REAP){assert(evidences==submits);reaps++;}return 0;
}
int main(){vxl_hunet_client c{};c.fd=123;c.loaded=1;c.handles[0]=1;c.handles[3]=4;uint64_t id;vxl_hunet_job_evidence_v1 e{};
 for(unsigned i=1;i<=1023;i++){vxl_hunet_job_v1 j{};memcpy(&j,vxl_hunet_catalog_job(i),128);assert(!vxl_hunet_client_submit(&c,&j,&id));assert(!vxl_hunet_client_wait(&c,id,1000));assert(!vxl_hunet_client_evidence(&c,id,&e));assert(!vxl_hunet_client_reap(&c,id));}
 assert(submits==1023&&evidences==1023&&reaps==1023);fail_at=1024;vxl_hunet_job_v1 j{};assert(vxl_hunet_client_submit(&c,&j,&id)==-EIO);assert(vxl_hunet_client_submit(&c,&j,&id)!=0);assert(submits==1024);puts("HUNET_TYPED_CLIENT_1023_SUBMIT6_RECEIPT_REAP_FAIL_STOP_PROTOCOL_ONLY");
 c.stopped=0;c.last_job=0;id=0;output_fault=true;
 assert(vxl_hunet_client_submit(&c,&j,&id)==-EFAULT);assert(c.stopped&&c.last_job==2048&&id==2048&&statuses==1);
 assert(vxl_hunet_client_submit(&c,&j,&id)!=0&&submits==1025);assert(!vxl_hunet_client_evidence(&c,c.last_job,&e));assert(e.job_id==2048);
 puts("START_THEN_COPYOUT_FAULT_RECOVER_STATUS_WITHOUT_RESUBMIT_PASS");
}
