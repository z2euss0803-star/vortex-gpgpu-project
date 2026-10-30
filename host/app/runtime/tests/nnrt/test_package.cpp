#include "fixture.hpp"
#include <thread>
int main(int argc,char**argv){
 assert(argc==2);auto c=canonical_metadata("{\"z\":18446744073709551615,\"é\":\"中文/\\n\",\"a\":[]}");
 require(bool(c)&&*c.value=="{\"a\":[],\"z\":18446744073709551615,\"é\":\"中文/\\n\"}","canonical unicode/integer golden");
 for(const auto&s:{"{\"a\":1,\"a\":2}","{\"a\":1,\"\\u0061\":2}","{\"n\":NaN}","{\"n\":1.25}","{\"n\":18446744073709551616}","{\"n\":-9223372036854775809}","{\"n\":01}","{}[]"})require(!canonical_metadata(s),"strict JSON rejected");
 require(digest_hex(content_digest("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA independent golden");
 auto p=load_package(Path(argv[1])/"valid");require(bool(p)&&p.value->assets().size()==4&&p.value->resident_bytes()>0,"valid immutable package");
 for(const char*n:{"duplicate-key","unknown-field","nan","path-escape","asset-missing","asset-drift","float-metadata","bad-limits","unlisted-file","writable-file","symlink","hardlink","fifo","bad-id","bad-utf8","unlisted-empty-directory"}){auto bad=load_package(Path(argv[1])/n);require(!bad&&bad.error->category=="PACKAGE_REJECTED",n);}
 PackageLimits lim;lim.max_total_bytes=16;require(!load_package(Path(argv[1])/"valid",lim),"total bound");lim={};lim.max_json_bytes=16;require(!load_package(Path(argv[1])/"valid",lim),"json bound");lim={};lim.max_files=3;require(!load_package(Path(argv[1])/"valid",lim),"file count bound");
 std::vector<std::thread> readers;for(unsigned i=0;i<4;i++)readers.emplace_back([&]{for(unsigned j=0;j<40;j++){auto copy=load_package(Path(argv[1])/"valid");require(bool(copy)&&copy.value->model_id()==p.value->model_id(),"four reader immutable package identity");}});for(auto&t:readers)t.join();
 puts("U07_PACKAGE_PASS_OFFLINE_GPU_CYCLES_NULL");
}
