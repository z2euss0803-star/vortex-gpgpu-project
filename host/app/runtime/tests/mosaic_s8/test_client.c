#define _POSIX_C_SOURCE 200809L
#define MOSAIC_IOCTL_EMBED_TEST
#include "test_ioctl.c"
#include <vxl/mosaic_client.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
static int (*execute_hook)(const struct mosaic_job_v1 *,unsigned);
static unsigned code_loads,weight_loads,submits;static int fault_submit;static unsigned corrupt_evidence;
int __wrap_ioctl(int fd,unsigned long cmd,...){
 assert(fd==55);va_list ap;va_start(ap,cmd);struct vxl_request *r=va_arg(ap,struct vxl_request *);va_end(ap);
 if(cmd==VXL_COPY_TO&&r->handle==e.objects[0].handle)++code_loads;
 if(cmd==VXL_COPY_TO&&r->handle==e.objects[1].handle)++weight_loads;
 if(cmd==VXL_WAIT&&h.job_active){int executed=execute_hook?execute_hook(&h.evidence.args,h.phase):0;done();if(executed)put32(f.ps+0x2008,(vxl_u32)-executed);if(fault_submit==(int)submits)f.data[0]^=1;}
 if(cmd==VXL_SUBMIT){++submits;h.submit_enter_ns=now(NULL);}
 int rc=call((vxl_u32)cmd,r);struct mosaic_delivery ticket;mosaic_delivery_capture(&c,&ticket);if(!rc&&ticket.serial)rc=mosaic_delivery_finish(&c,&ticket,1);
 if(!rc&&cmd==VXL_MOSAIC_EVIDENCE&&corrupt_evidence){vxl_u8 *wire=(void *)(uintptr_t)r->user_ptr;if(corrupt_evidence==264)memset(wire+264,0,8);else put32(wire+corrupt_evidence,corrupt_evidence==244?0x4770:corrupt_evidence==188?4:1);}
 if(rc){errno=-rc;return -1;}return 0;
}
int mosaic_client_test_main(int argc,char **argv){assert(argc==2);setup(argv[1]);e.ops=&ops;memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;
 struct mosaic_client client;assert(!mosaic_client_attach_fd(&client,55));assert(client.generation==9&&client.fd==55);
 assert(mosaic_client_configure(&client,&program,f.weights,3382783,0)<0&&!e.objects[0].handle);
 struct mosaic_program wrong=program;wrong.binding_sha[0]^=1;assert(mosaic_client_configure(&client,&wrong,f.weights,3382784,0)<0);
 assert(!mosaic_client_configure(&client,&program,f.weights,3382784,0));struct mosaic_manifest bad=fixture_manifest;bad.package_sha[0]^=1;assert(mosaic_client_load(&client,&bad)==-ESTALE&&!e.objects[0].handle);
 assert(!mosaic_client_load(&client,&fixture_manifest)&&code_loads==1&&weight_loads==1);
 vxl_u8 input[1152]={0},output[1152];FILE *journal=tmpfile();assert(journal);assert(!mosaic_client_frame(&client,input,sizeof(input),output,sizeof(output),fileno(journal)));
 struct demo_run *run=calloc(1,sizeof(*run));struct demo_result *result=calloc(1,sizeof(*result));assert(run&&result);run->app=DEMO_MOSAIC;run->generation=9;strcpy(run->run_id,"11111111111111111111111111111111");
 assert(!mosaic_client_run(&client,run,result));assert(submits==77&&client.epoch==1&&h.epoch==1&&client.metrics.jobs==77&&client.metrics.raw_cycles==77000);assert(!memcmp(input,output,1152));
 strcpy(run->run_id,"22222222222222222222222222222222");assert(!mosaic_client_run(&client,run,result));assert(submits==154&&client.epoch==2&&h.epoch==2&&code_loads==1&&weight_loads==1);assert(!memcmp(input,output,1152));assert(ftell(journal)==154*512);
 assert(!mosaic_client_close(&client)&&client.fd==55);assert(fcntl(fileno(journal),F_GETFD)>=0);fclose(journal);free(run);free(result);cleanup();
 setup(argv[1]);e.ops=&ops;memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;assert(!mosaic_client_attach_fd(&client,55));assert(!mosaic_client_configure(&client,&program,f.weights,3382784,0));assert(!mosaic_client_load(&client,&fixture_manifest));journal=tmpfile();assert(journal);assert(!mosaic_client_frame(&client,input,1152,output,1152,fileno(journal)));run=calloc(1,sizeof(*run));result=calloc(1,sizeof(*result));run->app=DEMO_MOSAIC;run->generation=9;strcpy(run->run_id,"33333333333333333333333333333333");fault_submit=submits+3;unsigned before=submits;assert(mosaic_client_run(&client,run,result)<0);assert(submits==before+3&&client.first_error&&e.quarantined);int first_error=client.first_error;strcpy(run->run_id,"44444444444444444444444444444444");assert(mosaic_client_run(&client,run,result)==first_error&&submits==before+3);unsigned old_frees=frees;assert(mosaic_client_close(&client)==first_error&&frees==old_frees&&client.fd==55);assert(ftell(journal)==3*512);fclose(journal);free(run);free(result);cleanup();
 const unsigned corruptions[]={264,244,260,188};
 for(unsigned n=0;n<4;++n){fault_submit=0;setup(argv[1]);e.ops=&ops;memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;assert(!mosaic_client_attach_fd(&client,55));assert(!mosaic_client_configure(&client,&program,f.weights,3382784,0));assert(!mosaic_client_load(&client,&fixture_manifest));journal=tmpfile();assert(journal);assert(!mosaic_client_frame(&client,input,1152,output,1152,fileno(journal)));run=calloc(1,sizeof(*run));result=calloc(1,sizeof(*result));run->app=DEMO_MOSAIC;run->generation=9;strcpy(run->run_id,"99999999999999999999999999999999");corrupt_evidence=corruptions[n];unsigned before=submits;assert(mosaic_client_run(&client,run,result)<0);assert(submits==before+1&&client.first_error&&e.job.state==VXL_COMPLETED);assert(ftell(journal)==512);unsigned old_frees=frees;assert(mosaic_client_close(&client)<0&&frees==old_frees);fclose(journal);free(run);free(result);corrupt_evidence=0;cleanup();}
 puts("CLIENT_RESIDENT_EPOCH_77_BATCH_FIRST_GUARD_FAULT_PIN_BORROWED_FD_PASS_MODEL_RAW_ONLY_NOT_NUMERIC_NOT_BOARD");return 0;}

#ifndef MOSAIC_SESSION_SHIM
int main(int argc,char **argv){return mosaic_client_test_main(argc,argv);}
#else
void mosaic_test_setup(const char *path){setup(path);e.ops=&ops;memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;}
void mosaic_test_cleanup(void){cleanup();}
const struct mosaic_program *mosaic_test_program(void){return &program;}
void *mosaic_test_map(vxl_u64 a,vxl_u64 n){return map(a,n);}
void mosaic_test_execute(int (*fn)(const struct mosaic_job_v1 *,unsigned)){execute_hook=fn;}
unsigned mosaic_test_submits(void){return submits;}
#endif
