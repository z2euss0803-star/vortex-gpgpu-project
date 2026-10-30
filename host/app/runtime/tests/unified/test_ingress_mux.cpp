#include <vxunified/ingress_mux.hpp>
#include <cassert>
#include <cstdio>
using namespace vxunified;
template<class T>auto put_settle(T&s,std::function<Result<void>()>f,int)->decltype(s.settle_native=std::move(f),void()){s.settle_native=std::move(f);}
template<class T>void put_settle(T&,std::function<Result<void>()>,long){}
template<class T>auto settle(T&s,int)->decltype(s.settle_native()){return s.settle_native?s.settle_native():Result<void>::success();}
template<class T>Result<void>settle(T&,long){return Result<void>::failure({ENOSYS,"TEST","settle hook absent"});}
int main(){
 ApplicationSet left,right;std::vector<int>calls;bool fail=false;int idle=0;
 left.poll_native=[&](uint32_t ms){assert(ms<=25);calls.push_back(1);return Result<void>::success();};
 right.poll_native=[&](uint32_t ms){assert(ms==0);calls.push_back(2);return fail?Result<void>::failure({EIO,"RIGHT","original socket error"}):Result<void>::success();};
 left.prepare_native=[&](){calls.push_back(3);return Result<bool>::success(idle==0);};right.prepare_native=[&](){calls.push_back(4);return Result<bool>::success(true);};
 left.activate=[&](){calls.push_back(5);return Result<void>::success();};right.activate=[&](){calls.push_back(6);return Result<void>::success();};
 left.quiesce=[&](){calls.push_back(7);return Result<void>::success();};right.quiesce=[&](){calls.push_back(8);return Result<void>::success();};
 left.release_resources=[&](){calls.push_back(9);return Result<void>::success();};right.release_resources=[&](){calls.push_back(10);return Result<void>::success();};
 bool fail_settle=false;put_settle(left,[&](){calls.push_back(11);return fail_settle?Result<void>::failure({EBUSY,"LEFT_SETTLE","original retained root"}):Result<void>::success();},0);put_settle(right,[&](){calls.push_back(12);return Result<void>::success();},0);
 TrustedApplication a;a.id="nn";left.entries.push_back(a);a.id="p8";right.entries.push_back(a);
 assert(merge_native_ingress(left,std::move(right)));assert(left.entries.size()==2);
 assert(settle(left,0));assert((calls==std::vector<int>{11,12}));calls.clear();fail_settle=true;auto stop=settle(left,0);assert(!stop&&stop.error->code==EBUSY&&stop.error->category=="LEFT_SETTLE"&&(calls==std::vector<int>{11}));calls.clear();fail_settle=false;
 assert(left.activate());assert((calls==std::vector<int>{5,6}));calls.clear();
 assert(left.poll_native(500));assert((calls==std::vector<int>{1,2}));calls.clear();
 assert(*left.prepare_native().value);assert(*left.prepare_native().value);assert((calls==std::vector<int>{3,4}));calls.clear();
 idle=1;assert(*left.prepare_native().value);assert((calls==std::vector<int>{3,4}));calls.clear();
 fail=true;auto error=left.poll_native(0);assert(!error&&error.error->code==EIO&&error.error->detail=="original socket error");calls.clear();
 assert(left.quiesce());assert(left.release_resources());assert((calls==std::vector<int>{8,7,10,9}));
 for(unsigned side=0;side<2;++side){ApplicationSet first,second;unsigned count=0;first.poll_native=second.poll_native=[](uint32_t){return Result<void>::success();};first.prepare_native=second.prepare_native=[](){return Result<bool>::success(false);};auto hook=[&](){++count;return Result<void>::success();};if(side)put_settle(second,hook,0);else put_settle(first,hook,0);assert(merge_native_ingress(first,std::move(second))&&settle(first,0)&&count==1);}
 ApplicationSet bad;bad.entries.push_back(a);assert(!merge_native_ingress(left,std::move(bad)));assert(left.entries.size()==2);
 puts("JOINT_SINGLE_WORKER_TWO_INGRESS_FAIRNESS_LIFECYCLE_FIRST_ERROR_PASS_NOT_BOARD");
}
