#include "scheduler_fixture.hpp"
#include <cmath>
#include <cstring>
int main(){
 auto b=std::make_shared<PendingBudget>();vxnn::CpuPool pool(4,32,b);auto token=value(b->reserve(4),"existing GELU reservation");auto other=value(b->reserve(28),"other common pending work");float x[11],y[11],scalar[11];for(int i=0;i<11;i++){x[i]=(i-5)*.37f;scalar[i]=(x[i]*.5f)*(1.0f+erff(x[i]/1.4142135623730951f));}
 auto result=vxnn::gelu_erf_pool_reserved(pool,x,y,11,{},token);require(bool(result),"GELU must use original four tokens with global32 already full");require(std::memcmp(y,scalar,sizeof(y))==0,"borrowed GELU original scalar expression");require(b->used()==32,"GELU cannot release scheduler reservation");ok(b->release(token),"owner retires GELU");ok(b->release(other),"other work retires");require(b->used()==0,"no borrowed GELU token leak");
 std::puts("U09_GELU_BORROWED_TOKEN_ORIGINAL_EXPRESSION_PASS_NOT_BOARD");
}
