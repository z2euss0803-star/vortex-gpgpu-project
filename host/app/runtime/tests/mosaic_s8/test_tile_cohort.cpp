#include "device/mosaic_s8/tile.h"
#include "device/mosaic_s8/graph_internal.h"
#include "tests/mosaic_s8/fake_s8.h"
#include <limits>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <cerrno>
static void ck(bool v,const char*s){if(!v){fprintf(stderr,"COHORT_ASSERT %s\n",s);exit(78);}}
struct Cohort;
struct Lane {Cohort*c;unsigned id;const int16_t*a=nullptr,*b=nullptr;uint32_t k=0;int local=0;};
struct Cohort {
 std::mutex mu;std::condition_variable cv;unsigned mask=0,leaving=0;char kind=0;uint32_t argument=0;
 bool broken=false,active=false,override_sums=false,bad_begin=false,bad_ready=false,bad_drain=false,bad_release=false,bad_idle=false,never_ready=false;
 unsigned begins=0,bulk_calls=0,releases=0,steps=0,status_reads=0,read_pages=0,barriers=0,agreements=0;
 int fail_page=-1,fail_step=-1,fail_begin_lane=-1,fail_bulk_lane=-1,fail_release_lane=-1,first_error_lane=2;
 uint32_t k=0,accepted=0,bitmap=0;uint64_t epoch=0;std::atomic<uint64_t> ticks{0};
 std::array<Lane,4> lane;int rc[4]={};int values[4]={};const int16_t*aa[4]={};const int16_t*bb[4]={};
 mosaic_words4 reply{};alignas(64) uint32_t raw_guard[128];int64_t sums[64]={};uint32_t packed[96]={};
 Cohort(){for(unsigned l=0;l<4;l++)lane[l]={this,l};for(auto&v:raw_guard)v=0xa55a1367;}
 uint32_t*raw(){return raw_guard+16;}
 void pack(){memset(packed,0,sizeof(packed));for(unsigned i=0;i<64;i++){uint64_t v=uint64_t(sums[i])&0xffffffffffffULL;for(unsigned b=0;b<48;b++)if((v>>b)&1)packed[(i*48+b)/32]|=1u<<((i*48+b)%32);}}
 int enter(Lane*l,char tag,uint32_t arg,mosaic_words4*out=nullptr){
  std::unique_lock<std::mutex> lock(mu);auto wait=[&](auto pred){if(!cv.wait_for(lock,std::chrono::seconds(2),pred)){broken=true;cv.notify_all();return false;}return !broken;};
  if(!wait([&]{return broken||!leaving;}))return -ETIMEDOUT;
  if(!mask){kind=tag;argument=arg;}else if(kind!=tag||argument!=arg){broken=true;cv.notify_all();return -EPROTO;}
  if(mask&(1u<<l->id)){broken=true;cv.notify_all();return -EPROTO;}
  values[l->id]=l->local;aa[l->id]=l->a;bb[l->id]=l->b;mask|=1u<<l->id;
  if(mask==15){
   for(auto&v:rc)v=0;
   if(tag=='A'){++agreements;int first=0;for(int v:values)if(!first&&v)first=v;for(auto&v:rc)v=first;}
   else if(tag=='Y'){++barriers;}
   else if(tag=='B'){
    ++begins;if(active){for(auto&v:rc)v=-EBUSY;}else{active=true;k=arg;accepted=bitmap=0;memset(sums,0,sizeof(sums));reply={{bad_begin?1u:0u,0,k,1}};}
    if(fail_begin_lane>=0)rc[fail_begin_lane]=-EIO;
   }else if(tag=='S'){
    for(uint32_t t=0;t<arg;t++){
     if(int(t)==fail_step){rc[first_error_lane]=-ERANGE;break;}
     int16_t a[8],b[8];for(unsigned j=0;j<8;j++){a[j]=aa[j/2][t*8+j];b[j]=bb[j/2][t*8+j];}
     for(unsigned r=0;r<8;r++)for(unsigned c=0;c<8;c++)sums[r*8+c]+=int64_t(a[r])*b[c];++accepted;++steps;
    }
    if(override_sums){const int64_t edge[]={-(INT64_C(1)<<47),(INT64_C(1)<<47)-1,-1,0,1,INT64_C(0x123456789ab),-INT64_C(0x123456789ab)};for(unsigned i=0;i<64;i++)sums[i]=edge[i%7];}pack();
   }else if(tag=='P'){
    ++bulk_calls;for(unsigned p=0;p<24;p++){++read_pages;if(int(p)==fail_page){rc[first_error_lane]=-EIO;break;}bitmap|=1u<<p;}
    if(fail_bulk_lane>=0)rc[fail_bulk_lane]=-ENODATA;
   }else if(tag=='R'&&arg==31){
    ++status_reads;if(!active)reply={{bad_idle?1u:0u,0,0,0}};
    else if(bitmap==0xffffff)reply={{bad_drain?1u:0u,accepted,k,7}};
    else reply={{bad_ready?99u:(never_ready?4u:0u),accepted,k,never_ready?1u:3u}};
   }else if(tag=='R'&&arg<24){
    ++read_pages;if(int(arg)==fail_page){rc[first_error_lane]=-EIO;}else{bitmap|=1u<<arg;for(unsigned j=0;j<4;j++)reply.word[j]=packed[arg*4+j];}
   }else if(tag=='F'){
    ++releases;reply={{bad_release?1u:0u,accepted,k,7}};if(bitmap!=0xffffff){for(auto&v:rc)v=-EPROTO;}else active=false;
    if(fail_release_lane>=0)rc[fail_release_lane]=-EIO;
   }else {broken=true;}
   leaving=4;cv.notify_all();
  }else if(!wait([&]{return broken||leaving;}))return -ETIMEDOUT;
  int value=broken?-EPROTO:rc[l->id];if(out)*out=reply;
  if(!--leaving){mask=0;++epoch;cv.notify_all();}return value;
 }
 static int begin(void*p,uint32_t k,mosaic_words4*v){auto*l=(Lane*)p;return l->c->enter(l,'B',k,v);}
 static int single(void*,const mosaic_words4*,const mosaic_words4*){return -ENOTSUP;}
 static int steps_bulk(void*p,const int16_t*a,const int16_t*b,uint32_t k){auto*l=(Lane*)p;l->a=a;l->b=b;return l->c->enter(l,'S',k);}
 static int read(void*p,uint32_t control,mosaic_words4*v){auto*l=(Lane*)p;return l->c->enter(l,'R',control,v);}
 static int read_bulk(void*p,uint32_t*out){auto*l=(Lane*)p;int rc=l->c->enter(l,'P',24);if(!rc)for(unsigned page=0;page<24;page++)out[page*4+l->id]=l->c->packed[page*4+l->id];return rc;}
 static int release(void*p,mosaic_words4*v){auto*l=(Lane*)p;return l->c->enter(l,'F',0,v);}
 static void fence(void*){std::atomic_thread_fence(std::memory_order_seq_cst);}
 static uint64_t now(void*p){return ((Lane*)p)->c->ticks.fetch_add(4)+4;}
 static int sync(void*p){auto*l=(Lane*)p;return l->c->enter(l,'Y',0);}
 static int agree(void*p,int value){auto*l=(Lane*)p;l->local=value;return l->c->enter(l,'A',0);}
 mosaic_s8_ops ops(unsigned id){mosaic_s8_ops o{&lane[id],begin,single,read,release,fence,now,0,0,id,4,0};o.steps=steps_bulk;
#ifdef MOSAIC_COOP_V3
  o.read_pages=read_bulk;o.synchronize=sync;o.agree=agree;o.raw_pages=raw();
#endif
  return o;
 }
};
struct Result {int rc[4]={};alignas(64)int64_t guard[80];Result(){for(auto&v:guard)v=INT64_C(0x5555444433332222);}int64_t*out(){return guard+8;}};
static void execute(Cohort&c,uint32_t k,Result&r,int bad_input=-1,bool deadline=false){
 std::vector<int16_t>a(size_t(k)*8+8),b(size_t(k)*8+8);
 for(size_t i=0;i<size_t(k)*8;i++){a[i]=int16_t(int((i*1877+8191)%65536)-32768);b[i]=int16_t(int((i*99+381)%65536)-32768);}
 std::thread workers[4];for(unsigned l=0;l<4;l++)workers[l]=std::thread([&,l]{auto io=c.ops(l);if(deadline)io.deadline_ticks=1;r.rc[l]=mosaic_tile8(int(l)==bad_input?nullptr:a.data(),b.data(),k,r.out(),&io);});for(auto&t:workers)t.join();
 ck(!c.broken,"four distinct participants same collective epoch");
 for(unsigned l=0;l<4;l++)ck(r.rc[l]==r.rc[0],"all lanes same error");
 for(unsigned i=0;i<8;i++)ck(r.guard[i]==INT64_C(0x5555444433332222)&&r.guard[i+72]==INT64_C(0x5555444433332222),"output guards");
 for(unsigned i=0;i<16;i++)ck(c.raw_guard[i]==0xa55a1367&&c.raw_guard[112+i]==0xa55a1367,"raw guards");
 if(!r.rc[0]){ck(!memcmp(r.out(),c.sums,512),"all64 I48 values");ck(c.bulk_calls==1&&c.read_pages==24,"one bulk read all24 pages");ck(c.begins==1&&c.steps==k&&c.releases==1&&!c.active,"one full drained tile");}
 else for(unsigned i=0;i<64;i++)ck(r.out()[i]==INT64_C(0x5555444433332222),"failure preserves entire output");
}

static void convolution(bool bad_coefficient){
 constexpr uint32_t kpad=32,ci=3,co=8,h=8,w=8,core=1;
 std::vector<int16_t>q(ci*h*w),weights(co*kpad),a(kpad*8),b(kpad*8);
 for(size_t i=0;i<q.size();i++)q[i]=int16_t(int(i%19)-9);
 for(size_t i=0;i<weights.size();i++)weights[i]=i%kpad<27?int16_t(int(i%13)-6):0;
 float coeff[co*2];for(unsigned i=0;i<co;i++){coeff[i*2]=0.03125f;coeff[i*2+1]=float(i)/7;}
 std::vector<float>expected(co*h*w,12345.75f),actual=expected;alignas(64)int64_t acc[64]={};
 mosaic_layer geometry{ci,co,h,w,3,1,1,kpad,q.data(),q.size()};mosaic_job_v1 job{};job.tile_first=1;job.tile_count=6;
 auto context=[&](mosaic_s8_ops io,float*out){mosaic_device_context c{};c.core=core;c.parameters[0]={weights.data(),weights.size(),coeff,co*2,0.0625f,nullptr,nullptr};c.io[core]=io;c.memory.quantized=q.data();c.memory.fp[1]=out;c.memory.a[core]=a.data();c.memory.b[core]=b.data();c.memory.tile[core]=acc;c.memory.tile_capacity=kpad*8;return c;};
 fake_s8 old;auto baseline=old.ops();baseline.core=core;auto base=context(baseline,expected.data());
 ck(!mosaic_convolve_range(&job,&base,&geometry),"legacy convolution reference");
 if(bad_coefficient)coeff[8]=std::numeric_limits<float>::quiet_NaN();
 Cohort sim;int rc[4]={};uint64_t counts[4]={};std::thread workers[4];
 for(unsigned lane=0;lane<4;lane++)workers[lane]=std::thread([&,lane]{auto io=sim.ops(lane);io.core=core;auto c=context(io,actual.data());rc[lane]=mosaic_convolve_range(&job,&c,&geometry);counts[lane]=c.tiles;});
 for(auto&t:workers)t.join();ck(!sim.broken,"conv collective same epochs");for(int v:rc)ck(v==rc[0],"conv same error");
 if(bad_coefficient){ck(rc[0]==-ERANGE,"nonleader restore error agreed");for(float v:actual)ck(v==12345.75f,"nonleader error before current tile writes");}
 else{ck(!rc[0],"cooperative convolution status");ck(!memcmp(expected.data(),actual.data(),actual.size()*4),"nonzero first range every float unchanged");ck(sim.bulk_calls==2&&sim.begins==2&&sim.releases==2&&sim.steps==54&&counts[0]==2,"two complete owned tiles no duplicate compute");}
}

int main(int argc,char**){
 if(argc>1&&false)return 1;
 unsigned tests=0;
 for(unsigned k:{1u,243u,1152u,2592u,4096u}){Cohort c;Result r;execute(c,k,r);ck(!r.rc[0],"normal status");++tests;if(argc>1)break;}
 if(argc>1)return 0;
 {Cohort c;c.override_sums=true;Result r;execute(c,1,r);ck(!r.rc[0],"signed48 cross-page boundaries");++tests;}
 for(int lane=0;lane<4;lane++){
  {Cohort c;Result r;execute(c,3,r,lane);ck(r.rc[0]==-EINVAL&&c.begins==0,"lane invalid input before BEGIN");++tests;}
  {Cohort c;c.fail_begin_lane=lane;Result r;execute(c,3,r);ck(r.rc[0]==-EIO&&c.releases==0,"begin lane fault no release");++tests;}
  {Cohort c;c.fail_bulk_lane=lane;Result r;execute(c,3,r);ck(r.rc[0]==-ENODATA&&c.releases==0,"bulk lane fault no release");++tests;}
  {Cohort c;c.fail_release_lane=lane;Result r;execute(c,3,r);ck(r.rc[0]==-EIO&&c.releases==1,"release error no retry");++tests;}
 }
 for(int p:{0,11,23}){Cohort c;c.fail_page=p;Result r;execute(c,3,r);ck(r.rc[0]==-EIO&&c.releases==0,"page error no release");++tests;}
 for(int kind=0;kind<7;kind++){Cohort c;Result r;int expected=-EPROTO;
  if(kind==0)c.bad_begin=true;if(kind==1)c.bad_ready=true;if(kind==2)c.bad_drain=true;if(kind==3)c.bad_release=true;if(kind==4)c.bad_idle=true;if(kind==5){c.never_ready=true;expected=-ETIMEDOUT;}if(kind==6){c.fail_step=1;expected=-ERANGE;}
  execute(c,3,r);ck(r.rc[0]==expected,"bad status, bounded poll or step fault");ck(c.releases==(kind==3||kind==4?1u:0u),"release only fully drained, no retries");++tests;
 }
 {Cohort c;Result r;execute(c,3,r,-1,true);ck(r.rc[0]==-ETIMEDOUT&&c.releases==0,"leader deadline agreed");++tests;}
 convolution(false);convolution(true);tests+=2;
 fprintf(stdout,"MOSAIC_COHORT_%u_CASES_REAL4THREAD_HOST_PROTOCOL_MATH_PASS_NOT_GPU_PERFORMANCE\n",tests);return 0;
}
