#include <vxunified/startup.hpp>
#include <cassert>
#include <cstdio>
using namespace vxunified;
int main(){
 std::string raw="{\"version\":1,\"private_dir\":\"/run/test\",\"evidence_dir\":\"/run/evidence\",\"device_path\":\"/dev/vx-compute\",\"backend_mode\":\"compute-v2\",\"legacy_profile\":0,\"device_generation\":1,\"provider_flags\":1";
 for(auto key:{"hardware_contract","bit","xsa","compatible"})raw+=",\""+std::string(key)+"\":\""+std::string(64,'1')+"\"";
 auto old=decode_daemon_config(raw+"}","boot");assert(old&&!old.value->fast_runtime);
 auto fast=decode_daemon_config(raw+",\"runtime_integrity\":\"fast\"}","boot");assert(fast&&fast.value->fast_runtime);
 auto strict=decode_daemon_config(raw+",\"runtime_integrity\":\"strict\"}","boot");assert(strict&&!strict.value->fast_runtime);
 assert(!decode_daemon_config(raw+",\"runtime_integrity\":\"anything\"}","boot"));
 auto legacy=raw;legacy.replace(legacy.find("compute-v2"),10,"legacy-compat");legacy.replace(legacy.find("\"legacy_profile\":0"),18,"\"legacy_profile\":4");assert(!decode_daemon_config(legacy+",\"runtime_integrity\":\"fast\"}","boot"));
 puts("R5_FAST_STARTUP_EXPLICIT_OPT_IN_OLD_CONFIG_STRICT_HOST_ONLY");
}
