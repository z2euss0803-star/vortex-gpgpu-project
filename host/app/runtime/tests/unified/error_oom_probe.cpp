// Independent reduction of SAN127: no controller, runtime, device or thread.
#include <vxnn/types.hpp>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
static std::atomic<int>fault{-1};static volatile size_t seen=0;
void*operator new(std::size_t n){if(fault.load()>=0&&fault.fetch_sub(1)==0){fault=-1;throw std::bad_alloc();}void*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
struct Pair{std::string a,b;};
struct ConstructedPair{std::string a,b;ConstructedPair(std::string x,std::string y):a(std::move(x)),b(std::move(y)) {}};
__attribute__((noinline))void consume(Pair p){seen+=p.a.size()+p.b.size();}
__attribute__((noinline))void consume_constructed(ConstructedPair p){seen+=p.a.size()+p.b.size();}
int main(int argc,char**argv){
 if(argc!=2){return 2;}
 std::string mode=argv[1];unsigned refused=0;
 for(int n=0;n<128;n++){
  fault=n;try{
   if(mode=="aggregate")consume({std::string(160,'c'),std::string(160,'e')});
   else if(mode=="constructed")consume_constructed({std::string(160,'c'),std::string(160,'e')});
   else if(mode=="named"){Pair p;p.a=std::string(160,'c');p.b=std::string(160,'e');consume(std::move(p));}
   else if(mode=="result-aggregate"){std::function<vxnn::Result<int>()>f=[](){return vxnn::Result<int>::failure({EINVAL,std::string(160,'c'),std::string(160,'e')});};auto r=f();if(!r)seen+=r.error->category.size();}
   else if(mode=="result-named"){std::function<vxnn::Result<int>()>f=[](){vxnn::Error e;e.code=EINVAL;e.category=std::string(160,'c');e.detail=std::string(160,'e');return vxnn::Result<int>::failure(std::move(e));};auto r=f();if(!r)seen+=r.error->category.size();}
   else return 2;
  }catch(const std::bad_alloc&){++refused;}fault=-1;
 }
 std::printf("U10_ERROR_OOM_COMPILER_PROBE mode=%s refused=%u seen=%zu NOT_RUNTIME_OR_BOARD\n",mode.c_str(),refused,size_t(seen));return refused==2?0:3;
}
