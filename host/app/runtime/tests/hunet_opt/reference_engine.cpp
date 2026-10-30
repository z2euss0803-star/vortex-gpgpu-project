/* Independent sequential host arithmetic model. Never includes candidate math,
 * never opens a device, and never pretends to execute a physical TCU. */
#include <vxl/hunet.h>
#include "../../generated/hunet_fused/catalog.h"
#include "../../generated/hunet/stages.h"
#include <cmath>
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <cstring>
namespace fs=std::filesystem;
static std::vector<uint8_t> weights;static std::vector<float> ws;static std::ofstream stats;
static std::vector<uint8_t> read(const fs::path&p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("READ");auto n=f.tellg();if(n<0)throw std::runtime_error("SIZE");std::vector<uint8_t>b(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);if(!f)throw std::runtime_error("SHORT_READ");return b;}
static void write(const fs::path&p,const void*b,size_t n){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(b),n);if(!f)throw std::runtime_error("WRITE");}
static float *tensor(uint32_t a,size_t n){if(a<0x85000000u||(a&3u)||uint64_t(a-0x85000000u)+n*4>ws.size()*4)throw std::runtime_error("WORKSPACE_SPAN");return ws.data()+(a-0x85000000u)/4;}
static float parameter(uint32_t a){if(a<0x80800000u||uint64_t(a-0x80800000u)+4>weights.size())throw std::runtime_error("PARAMETER_SPAN");float f;std::memcpy(&f,weights.data()+a-0x80800000u,4);return f;}
static float half(uint16_t h){unsigned e=(h>>10)&31,m=h&1023;float value=e?std::ldexp(float(1024+m),int(e)-25):std::ldexp(float(m),-24);if(e==31)throw std::runtime_error("NONFINITE_WEIGHT");return (h&0x8000)?-value:value;}
static float weight(uint32_t a){if(a<0x80800000u||uint64_t(a-0x80800000u)+2>weights.size())throw std::runtime_error("WEIGHT_SPAN");uint16_t h;std::memcpy(&h,weights.data()+a-0x80800000u,2);return half(h);}
static float rounded_half(float x){if(!std::isfinite(x))throw std::runtime_error("NONFINITE_PACK");float a=std::abs(x);if(a==0)return x;int e;std::frexp(a,&e);int step=a<std::ldexp(1.f,-14)?-24:e-11;float y=std::ldexp(std::nearbyint(std::ldexp(a,-step)),step);if(y>65504)throw std::runtime_error("HALF_OVERFLOW");return std::copysign(y,x);}
static void add(float x,float &s,float &c){float z=x-c,t=s+z;c=(t-s)-z;s=t;}
static std::pair<float,float> gn(const float*x,unsigned n,unsigned workers){
 float sum=0,carry=0,partials[64]={};
 for(unsigned w=0;w<workers;w++){float a=0,b=0;for(uint64_t i=uint64_t(n)*w/workers;i<uint64_t(n)*(w+1)/workers;i++)add(x[i],a,b);partials[w]=a;}
 for(unsigned w=0;w<workers;w++)add(partials[w],sum,carry);
 float mean=sum/float(n);sum=carry=0;
 for(unsigned w=0;w<workers;w++){float a=0,b=0;for(uint64_t i=uint64_t(n)*w/workers;i<uint64_t(n)*(w+1)/workers;i++){float z=x[i]-mean;add(z*z,a,b);}partials[w]=a;}
 for(unsigned w=0;w<workers;w++)add(partials[w],sum,carry);
 return {mean,sum/float(n)};
}
static void matrix(const HunetFusedStageV1&s){
 unsigned k=s.ci*s.kernel_k*s.kernel_k,n=s.oh*s.ow,rows=s.co,kp=(k+7)/8*8;
 if(s.flags==2){k=s.kernel_k;kp=(k+7)/8*8;rows=1;n=s.co;}
 const float*x=tensor(s.input_gpu,size_t(s.ci)*s.ih*s.iw);float*y=tensor(s.output_gpu,size_t(rows)*n);
 std::vector<float> packed(size_t(kp)*n,0);
 for(unsigned p=0;p<n;p++)for(unsigned kk=0;kk<k;kk++){
  float value=0;if(s.flags==2){int ix=int(p+kk)-int(s.pad);if(ix>=0&&ix<int(s.iw))value=x[ix];}
  else{unsigned channel=kk/(s.kernel_k*s.kernel_k),dy=(kk/s.kernel_k)%s.kernel_k,dx=kk%s.kernel_k;int iy=int((p/s.ow)*s.stride+dy)-int(s.pad),ix=int((p%s.ow)*s.stride+dx)-int(s.pad);if(iy>=0&&ix>=0&&iy<int(s.ih)&&ix<int(s.iw))value=x[channel*s.ih*s.iw+unsigned(iy)*s.iw+unsigned(ix)];}
  packed[size_t(kk)*n+p]=rounded_half(value);
 }
 for(unsigned row=0;row<rows;row++){
  std::vector<float>w(kp);for(unsigned kk=0;kk<kp;kk++)w[kk]=weight(s.weights_gpu+(row*kp+kk)*2);
  for(unsigned p=0;p<n;p++){float acc=0;for(unsigned kk=0;kk<kp;kk+=4){float p0=w[kk]*packed[size_t(kk)*n+p],p1=w[kk+1]*packed[size_t(kk+1)*n+p],p2=w[kk+2]*packed[size_t(kk+2)*n+p],p3=w[kk+3]*packed[size_t(kk+3)*n+p];float left=p0+p1,right=p2+p3;acc=acc+(left+right);}y[row*n+p]=s.bias_gpu?acc+parameter(s.bias_gpu+row*4):acc;}
 }
}
static void scalar(const vxl_hunet_job_v1&j,bool parallel){
 unsigned hw=j.ih*j.iw,n=j.ci*hw;float*x=tensor(j.x,n),*y=tensor(j.y,j.op==4?2:j.tasks);
 if(j.op==4){unsigned workers=parallel?(n<4096?1:n<65536?16:64):1;auto a=gn(x,n,workers),old=gn(x,n,1);double mean=0,var=0;for(unsigned i=0;i<n;i++)mean+=x[i];mean/=n;for(unsigned i=0;i<n;i++){double d=x[i]-mean;var+=d*d;}var/=n;float eps;std::memcpy(&eps,&j.epsilon_bits,4);y[0]=a.first;y[1]=1.f/std::sqrt(a.second+eps);stats<<j.stage_id<<','<<n<<','<<workers<<','<<a.first<<','<<a.second<<','<<old.first<<','<<old.second<<','<<mean<<','<<var<<'\n';return;}
 for(unsigned t=0;t<j.tasks;t++){
  switch(j.op){
   case 5:{float*st=tensor(j.scratch,2);y[t]=((x[t]-st[0])*st[1])*parameter(j.gamma+(t/hw)*4)+parameter(j.beta+(t/hw)*4);break;}
   case 6:y[t]=(x[t]*.5f)*(1.f+std::erf(x[t]/1.4142135623730951f));break;
   case 7:{unsigned c=t/hw,row=t%hw/j.iw,col=t%j.iw,g=j.ci/j.n_div;int iy=int(row),ix=int(col);if(c<g)ix++;else if(c<2*g)ix--;else if(c<3*g)iy++;else if(c<4*g)iy--;y[t]=iy>=0&&ix>=0&&iy<int(j.ih)&&ix<int(j.iw)?x[c*hw+unsigned(iy)*j.iw+unsigned(ix)]:0;break;}
   case 8:y[t]=x[t]+tensor(j.w,j.tasks)[t];break;
   case 9:{float sum=0,carry=0;for(unsigned i=0;i<hw;i++)add(x[t*hw+i],sum,carry);y[t]=sum/float(hw);break;}
   case 10:y[t]=1.f/(1.f+std::exp(-x[t]));break;
   case 11:y[t]=x[t]*tensor(j.w,j.co)[t/(j.oh*j.ow)];break;
   case 12:y[t]=x[j.groups*hw+t];break;
   case 13:y[t]=t<n?x[t]:tensor(j.w,j.tasks-n)[t-n];break;
   case 14:y[t]=x[t];break;
   default:throw std::runtime_error("OP");
  }
 }
}
int main(int argc,char**argv){try{
 if(argc==2&&!std::strcmp(argv[1],"--selftest")){for(unsigned h=0;h<65536;h++)if((h&0x7c00)!=0x7c00){float a=half(uint16_t(h));float b=rounded_half(a);assert(std::memcmp(&a,&b,4)==0);}float x[4097];for(unsigned i=0;i<4097;i++)x[i]=float(int(i%101)-50)/13;assert(std::isfinite(gn(x,4097,16).first));ws.resize(VXL_HUNET_WS_BYTES/4);weights.assign(32,0);uint16_t one=0x3c00;for(unsigned i=0;i<3;i++)std::memcpy(weights.data()+i*2,&one,2);
 HunetFusedStageV1 eca={};eca.ci=1;eca.co=5;eca.ih=eca.oh=eca.ow=1;eca.iw=5;eca.kernel_k=3;eca.pad=1;eca.flags=2;eca.input_gpu=0x85000040;eca.output_gpu=0x85000080;eca.weights_gpu=0x80800000;
 float *in=tensor(eca.input_gpu,5),*out=tensor(eca.output_gpu,6);for(unsigned i=0;i<5;i++)in[i]=float(i+1);out[5]=123;matrix(eca);const float expected[]={3,6,9,12,9,123};assert(!std::memcmp(out,expected,sizeof(expected)));
 puts("HOST_INDEPENDENT_REFERENCE_HALF_ROUNDTRIP_GN_ECA_ROW_LAYOUT_PASS_NOT_GPU");return 0;}
 if(argc!=5)throw std::runtime_error("ARGS");
 bool parallel=!std::strcmp(argv[4],"parallel");if(!parallel&&std::strcmp(argv[4],"ordered"))throw std::runtime_error("GN_POLICY");
 weights=read(argv[2]);if(weights.size()!=23685056)throw std::runtime_error("WEIGHT_SIZE");auto input=read(argv[1]);if(input.size()!=196608)throw std::runtime_error("INPUT_SIZE");ws.resize(VXL_HUNET_WS_BYTES/4);std::memcpy(tensor(VXL_HUNET_INPUT,49152),input.data(),input.size());fs::path out=argv[3];if(!fs::is_directory(out)||!fs::is_empty(out))throw std::runtime_error("FRESH_OUTPUT_REQUIRED");stats.open(out/"gn-stats.csv");stats.precision(17);stats<<"stage,elements,workers,new_mean,new_variance,ordered_mean,ordered_variance,fp64_mean,fp64_variance\n";
 unsigned stage=0;for(unsigned index=0;index<103;index++){
  auto*j=vxl_fused_jobs[index];unsigned last=j[4];if(j[3]==15){HunetFusedStageV1 s;std::memcpy(&s,j,128);matrix(s);last=s.last_logical_sequence;}
  else{vxl_hunet_job_v1 s;std::memcpy(&s,j,128);scalar(s,parallel);}
  auto save_stage=[&](){if(stage<100&&last==vxl_hunet_stages[stage].last_sequence){auto&s=vxl_hunet_stages[stage];float*p=tensor(s.gpu,s.bytes/4);for(unsigned t=0;t<s.bytes/4;t++)if(!std::isfinite(p[t]))throw std::runtime_error("NONFINITE_STAGE");char name[40];std::snprintf(name,sizeof(name),"%03u.f32",stage);write(out/name,p,s.bytes);stage++;}};save_stage();
  const auto*cpu=vxl_hunet_catalog_job(last+1);if(cpu&&cpu[3]==6){vxl_hunet_job_v1 s;std::memcpy(&s,cpu,128);scalar(s,parallel);last=s.sequence;save_stage();}
 }
 if(stage!=100)throw std::runtime_error("COVERAGE");
 stats.close();write(out/"logits.f32",tensor(VXL_HUNET_LOGITS,8105),32420);puts("HOST_NUMERIC_MODEL_100_STAGES_PROPAGATED_NOT_TCU_NOT_GPU_NOT_A53");return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"HOST_REFERENCE_FAIL_STOP: %s\n",e.what());return 1;}}
