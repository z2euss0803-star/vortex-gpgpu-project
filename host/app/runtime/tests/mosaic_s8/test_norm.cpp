#include "graph.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
int main(){
 float x[]={1,2,3,4},hi[4],lo[4],stats[3]={},out[4];mosaic_norm_workspace ws={hi,lo,4};
 assert(mosaic_norm_stats(x,4,&ws,stats)==0);
 assert(stats[0]==2.5f&&stats[1]==1.25f);
 assert(mosaic_norm_apply(x,4,stats,1,0,out)==0);
 assert(fabsf(out[0]-(-1.3416355f))<1e-6f);
 // Whole-channel statistics differ from independently normalized halves.
 float half[3],h2[2],l2[2];mosaic_norm_workspace short_ws={h2,l2,2};
 assert(mosaic_norm_stats(x,2,&short_ws,half)==0);assert(half[0]==1.5f&&half[1]==.25f);
 float special[]={100000000.f,1.f,-100000000.f,1.f};
 assert(mosaic_norm_stats(special,4,&ws,stats)==0);assert(stats[0]==.5f);
 float constant[]={7,7,7};assert(mosaic_norm_stats(constant,3,&ws,stats)==0);
 assert(mosaic_norm_apply(constant,3,stats,2,3,out)==0);for(unsigned i=0;i<3;++i)assert(out[i]==3);
 float bad[]={1,NAN,2};assert(mosaic_norm_stats(bad,3,&ws,stats)<0);
 assert(mosaic_norm_stats(x,4,&short_ws,stats)<0);
 assert(mosaic_norm_stats(x,0,&ws,stats)<0);
 mosaic_norm_workspace alias={hi,hi,4};assert(mosaic_norm_stats(x,4,&alias,stats)<0);
 stats[2]=NAN;assert(mosaic_norm_apply(x,4,stats,1,0,out)<0);
 puts("MOSAIC_NORM_SOFTWARE_PASS_NOT_NETWORK_NOT_GPU");
}
