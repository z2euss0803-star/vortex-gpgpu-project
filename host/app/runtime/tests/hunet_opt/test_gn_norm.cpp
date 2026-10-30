#define main gn_phase_previous_main
#include "test_gn_phases.cpp"
#undef main
#ifndef GN_NORM_HEADER
#define GN_NORM_HEADER "../../device/hunet_opt_gn_norm.h"
#endif
#include GN_NORM_HEADER
struct NormShared {Shared phases;unsigned visits[64]={};};
struct NormPort:Port {
 NormShared &norm;
 int spawn_norm(OptGnNormRun *r){for(uint32_t w=id*4;w<r->gn.workers;w+=16)for(uint32_t l=0;l<4&&w+l<r->gn.workers;l++){norm.visits[w+l]++;opt_gn_norm_worker(r,w+l);}return 0;}
};
static void norm_case(uint32_t channels,uint32_t hw,int fault){
 uint32_t n=channels*hw;NormShared shared;std::vector<float>x(n),y(n+2,123),gamma(channels),beta(channels);for(uint32_t i=0;i<n;i++)x[i]=float(int(i%401)-200)/31;
 for(uint32_t i=0;i<channels;i++)gamma[i]=float(i%7+1)/4,beta[i]=float(i%3)/8;
 if(fault==1)gamma[0]=INFINITY;
 OptGnScratch scratch={};std::memset(scratch.guard_left,0xa5,64);std::memset(scratch.guard_right,0xa5,64);if(fault==2)scratch.guard_right[0]=0;
 HunetJobV1 job={};job.ci=channels;job.ih=1;job.iw=hw;job.epsilon_bits=0x3727c5ac;float stats[2]={};int rc[4]={};std::thread threads[4];
 for(unsigned c=0;c<4;c++)threads[c]=std::thread([&,c]{NormPort port{{shared.phases,c,0},shared};rc[c]=opt_gn_norm_phases(job,x.data(),y.data()+1,stats,gamma.data(),beta.data(),&scratch,port);});
 for(auto&t:threads){t.join();}
 for(unsigned c=0;c<4;c++){assert(rc[c]==rc[0]);}
 assert(y.front()==123&&y.back()==123);
 if(fault){assert(rc[0]);return;}assert(!rc[0]);
 for(unsigned c=0;c<4;c++)assert(shared.phases.joins[c]==8&&shared.phases.fences[c]==8);
 for(uint32_t i=0;i<n;i++)assert(y[i+1]==((x[i]-stats[0])*stats[1])*gamma[i/hw]+beta[i/hw]);
 for(uint32_t w=0;w<opt_gn_workers(n);w++)assert(shared.visits[w]==1);
}
int main(){norm_case(1,1,0);norm_case(3,1367,0);norm_case(96,4096,0);norm_case(3,1367,1);norm_case(3,1367,2);puts("HOST_GN_NORM_SHARED_PHASES_EXACT_AFFINE_GUARDS_FINITE_PASS_NOT_GPU");}
