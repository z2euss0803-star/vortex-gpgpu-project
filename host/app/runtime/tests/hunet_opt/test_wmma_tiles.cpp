#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include "../../device/hunet_opt_wmma.h"
struct FragmentA {float a[8][8];};struct FragmentB {float b[8][4];};struct Acc {float a[8][4];};
struct Data {HunetFusedDescriptorV1 d;std::vector<uint16_t>w,b;std::vector<float>out,bias;std::vector<unsigned>owner,visits;unsigned loads=0;};
struct Port {
 using A=FragmentA;using B=FragmentB;using C=Acc;Data &v;unsigned lane,core,group;std::vector<std::vector<unsigned>> ks;
 void fill(C&a){std::memset(&a,0,sizeof(a));}
 void load_a(A&a,unsigned row,unsigned k){v.loads++;for(unsigned i=0;i<8;i++)for(unsigned q=0;q<8;q++)a.a[i][q]=hunet_unhalf(v.w[(row+i)*v.d.kp+k+q]);}
 void load_b(B&a,unsigned k,unsigned col){for(unsigned q=0;q<8;q++)for(unsigned j=0;j<4;j++)a.b[q][j]=hunet_unhalf(v.b[(k+q)*v.d.np+col+j]);}
 void mma(C&a,const A&w,const B&b,unsigned which,unsigned k){ks[which].push_back(k);for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)for(unsigned q=0;q<8;q++)a.a[i][j]+=w.a[i][q]*b.b[q][j];}
 void store(const C&a,unsigned row,unsigned col,unsigned tile){
  for(unsigned reg=0;reg<8;reg++){unsigned rr=lane/2+(reg/2)*2,cc=lane%2+(reg%2)*2;hunet_fused_store(v.d,v.out.data(),v.bias.data(),row,col,lane,reg,a.a[rr][cc]);}
  if(!lane){v.owner[tile]=core+1;v.visits[tile]++;}
 }
 void loop_begin(){}void loop_end(){}void store_begin(){}void store_end(){}
};
template<unsigned T> static void run(unsigned np,unsigned kp){
 Data v;v.d={};v.d.m=13;v.d.mp=16;v.d.n=np-1;v.d.np=np;v.d.k=kp-3;v.d.kp=kp;v.d.count=np-2;v.d.offset=1;
 v.w.resize(v.d.mp*kp);v.b.resize(kp*np);v.bias.resize(v.d.m);v.out.resize(v.d.m*v.d.n,-999);unsigned tiles=v.d.mp/8*(np/4);v.owner.resize(tiles);v.visits.resize(tiles);
 for(unsigned i=0;i<v.w.size();i++)v.w[i]=hunet_half(float(int(i%11)-5)/8);
 for(unsigned i=0;i<v.b.size();i++)v.b[i]=hunet_half(float(int(i%13)-6)/16);
 for(unsigned i=0;i<v.bias.size();i++)v.bias[i]=float(i)/32;
 unsigned groups=opt_gemm_groups(v.d,T),per_row=(np/4+T-1)/T;
 for(unsigned g=0;g<groups;g++)for(unsigned lane=0;lane<4;lane++){
  Port p{v,lane,opt_expected_core(g,groups),g,std::vector<std::vector<unsigned>>(T)};
  opt_wmma_group<T>(v.d,g/per_row*8,g%per_row,lane,p);
  unsigned live=std::min(T,np/4-g%per_row*T);
  for(unsigned t=0;t<live;t++){assert(p.ks[t].size()==kp/8);for(unsigned q=0;q<kp/8;q++)assert(p.ks[t][q]==q*8);}
 }
 assert(v.loads==groups*4*(kp/8));
 for(unsigned row=0;row<v.d.m;row++)for(unsigned col=0;col<v.d.count;col++){
  float ref=0;for(unsigned k=0;k<kp;k++)ref+=hunet_unhalf(v.w[row*kp+k])*hunet_unhalf(v.b[k*np+col]);ref+=v.bias[row];assert(v.out[row*v.d.n+v.d.offset+col]==ref);
 }
 for(unsigned tile=0;tile<tiles;tile++){unsigned row=tile/(np/4),col=tile%(np/4),group=row*per_row+col/T;assert(v.visits[tile]==1&&v.owner[tile]==opt_expected_core(group,groups)+1);}
}
int main(){for(unsigned n:{4u,8u,12u,20u,36u})for(unsigned k:{8u,16u,24u}){run<1>(n,k);run<2>(n,k);run<4>(n,k);}
 const unsigned oracle17[]={0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3};for(unsigned g=0;g<17;g++)assert(opt_expected_core(g,17)==oracle17[g]);
 HunetFusedDescriptorV1 ds[2]={};ds[0].mp=16;ds[0].np=12;ds[1].mp=8;ds[1].np=20;
 uint32_t prefix[33];assert(opt_group_prefixes(ds,2,4,prefix)==4);assert(prefix[1]==2);
 assert(opt_find_group(prefix,2,0)==0&&opt_find_group(prefix,2,1)==0&&opt_find_group(prefix,2,2)==1&&opt_find_group(prefix,2,3)==1&&opt_find_group(prefix,2,4)==-1);
 assert(opt_expected_core(0,1)==0&&opt_expected_core(7,8)==1&&opt_expected_core(16,17)==3);assert(opt_expected_core(1,1)==UINT32_MAX);
 assert(opt_group_tile(UINT32_MAX,1,4)==UINT32_MAX);puts("HOST_ACTUAL_MULTI_TILE_LOOP_K_ORDER_WEIGHT_REUSE_OWNER_TAIL_PASS_NOT_TCU_NUMERICS_NOT_GPU");}
