#define MOSAIC_SESSION_SHIM
#include "test_client.c"
int main(int argc,char **argv){
 assert(argc==2);setup(argv[1]);e.ops=&ops;memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;
 struct mosaic_client client;assert(!mosaic_client_attach_fd(&client,55));assert(!mosaic_client_configure(&client,&program,f.weights,3382784,0));assert(!mosaic_client_load(&client,&fixture_manifest));
 vxl_u32 limits[6]={1,4,4096,4096,4096,4096};assert(!mosaic_client_set_batch_policy(&client,limits));
 vxl_u8 input[1152]={0},output[1152];memset(output,0x42,sizeof(output));FILE *journal=tmpfile();assert(journal);assert(!mosaic_client_frame(&client,input,sizeof(input),output,sizeof(output),fileno(journal)));
 struct demo_run *run=calloc(1,sizeof(*run));struct demo_result *result=calloc(1,sizeof(*result));assert(run&&result);run->app=DEMO_MOSAIC;run->generation=9;strcpy(run->run_id,"55555555555555555555555555555555");unsigned before=submits;
 assert(mosaic_client_run(&client,run,result)==-E2BIG);assert(submits==before&&!client.frame_active&&!client.first_error);
 assert(!mosaic_client_set_frame_budget(&client,1,512));assert(mosaic_client_run(&client,run,result)==-E2BIG&&submits==before);
 assert(!mosaic_client_set_prefix_budget(&client,8,512));
 assert(mosaic_client_run(&client,run,result)==-EINPROGRESS);
 assert(submits==before+1&&client.frame_active&&client.prefix_stopped&&!client.first_error&&client.metrics.jobs==1);
 assert(!h.job_active&&!h.evidence_pending&&h.frame_active&&e.job.state==VXL_READY&&!e.job.pinned);
 assert(ftell(journal)==512);for(unsigned i=0;i<sizeof(output);++i)assert(output[i]==0x42);
 assert(mosaic_client_run(&client,run,result)==-EBUSY&&submits==before+1);
 assert(mosaic_client_set_batch_policy(&client,limits)==-EBUSY);assert(mosaic_client_set_prefix_budget(&client,1,512)==-EBUSY);
 assert(mosaic_client_close(&client)==-EBUSY);
 puts("DIAGNOSTIC_PREFIX_STOPPED_NOT_FRAME_COMPLETE_HOST_PROTOCOL_TEST_GPU_RAW_CYCLES_NULL");
 fclose(journal);free(run);free(result);cleanup();return 0;
}
