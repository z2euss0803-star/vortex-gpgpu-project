#define main old_strict_hunet_tests
#include "test_hunet_migration.cpp"
#undef main
namespace {
struct FastBackend final:Backend {
 using Backend::Backend;
 bool fast_runtime()const override{return true;}
};
void fast_cpu(){
 auto budget=std::make_shared<PendingBudget>();CpuPool pool(4,32,budget);
 auto token=value(budget->reserve(4),"FAST borrowed CPU4 reservation");
 FastBackend b;HunetGeluReceipt r;ok(execute_hunet_gelu(task(),b,pool,{},token,r),"FAST CPU computes and uploads");
 require(b.downloads==1&&b.uploads==1&&r.hash_ns==0&&r.readback_ns==0,"FAST removes three full hashes and redundant output readback");
 require(r.committed&&!r.verified&&r.content_checks_omitted&&r.input_sha==Digest{}&&r.output_sha==Digest{}&&r.readback_sha==Digest{},"unchecked content never claims strict verification");
 for(size_t i=0;i<b.input.size();++i){float x=b.input[i],expected=(x*.5f)*(1.f+erff(x/1.4142135623730951f));require(!memcmp(&expected,&b.output[i],4),"FAST preserves CPU arithmetic bitwise");}
 FastBackend failed;failed.fail_upload=true;HunetGeluReceipt f;require(!execute_hunet_gelu(task(),failed,pool,{},token,f)&&!f.committed,"FAST propagates upload failure");
 FastBackend nonfinite;nonfinite.input[0]=INFINITY;HunetGeluReceipt n;require(!execute_hunet_gelu(task(),nonfinite,pool,{},token,n)&&!n.committed,"FAST finite CPU math gate retained");
 ok(budget->release(token),"caller releases original CPU token");
}
}
int main(){fast_cpu();chain();failures();puts("R5_FAST_CPU_NO_REPEAT_HASH_READBACK_AND_ORIGINAL_STRICT_REGRESSION_HOST_ONLY");}
