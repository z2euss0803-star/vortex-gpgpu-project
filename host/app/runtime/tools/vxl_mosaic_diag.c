/* Called by a source-bound owner after normal attach/configure/load/frame.
 * This entry never opens another fd or clears a live diagnostic session. */
#include <vxl/mosaic_client.h>
#include <unistd.h>
int vxl_mosaic_diag(struct mosaic_client *client,const struct demo_run *run,struct demo_result *result,vxl_u32 convolution_limit){
 if(!client||!run||!result||!client->loaded||client->frame_active||client->first_error)return -EINVAL;
 if(convolution_limit!=4&&convolution_limit!=16&&convolution_limit!=64&&convolution_limit!=256)return -ERANGE;
 vxl_u32 limits[6]={4096,convolution_limit,4096,4096,4096,4096};
 int rc=mosaic_client_set_batch_policy(client,limits);
 if(!rc)rc=mosaic_client_set_prefix_budget(client,8,4096);
 if(!rc)rc=mosaic_client_run(client,run,result);
 int saved=rc;if(client->evidence_fd>=0&&fsync(client->evidence_fd)&&(saved==0||saved==-EINPROGRESS)){
  saved=-errno;if(!client->first_error)client->first_error=saved;
 }
 /* EINPROGRESS is a held diagnostic prefix, never a successful frame. */
 return saved;
}
