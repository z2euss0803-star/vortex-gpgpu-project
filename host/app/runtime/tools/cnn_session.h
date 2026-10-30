#ifndef CNN_SESSION_H
#define CNN_SESSION_H
#include <vxl/transport.h>
#include "cnn_reference.h"
#include "cnn_timing.h"
#include <array>
#include <string>
#include <vector>
struct CnnIo {void *ctx=nullptr;int (*call)(void *,uint32_t,vxl_request *)=nullptr;int (*close)(void *)=nullptr;};
struct CnnObject {uint64_t handle=0;uint32_t gpu=0,count=0;std::vector<uint32_t> initial;};
struct CnnTransfer {unsigned object=0;bool write=false;uint64_t bytes=0,payload_bytes=0,confirmed_bytes=0;CnnInterval time;int result=0;};
struct CnnLayer {
 uint32_t program_id=0;uint64_t job_id=0;vxl_request snapshot{};bool snapshot_valid=false,guard_pass=false;
 uint64_t mismatch_count=0,max_abs=0;int result=-61;
 uint64_t code_h2d_bytes=0,args_h2d_bytes=0,d2h_bytes=0;
 std::array<bool,9> raw_valid{};
 CnnInterval code_args,submit_wait,verify;
 std::array<std::vector<uint32_t>,9> raw;
};
struct CnnIteration {
 std::string phase;unsigned iteration=0;int result=-61;CnnInterval audit,cpu_reference;
 std::array<CnnLayer,4> layers;std::vector<CnnTransfer> transfers;
 uint64_t kernel_chain_host_ns=0,tensor_h2d_ns=0,tensor_d2h_ns=0,h2d_bytes=0,d2h_bytes=0;
};
struct CnnRun {
 std::string scope,build_id,boot_id,kernel,source_commit,utc_start,environment,manifest_json;
 CnnNowFn now=cnn_monotonic_now;void *clock_ctx=nullptr;
 CnnInterval setup,teardown;uint64_t clock_resolution_ns=0;
 vxl_request initial_info{},final_info{};bool final_info_valid=false,pinned_retained=false;
 int result=-61,cleanup_result=-61,close_result=-61,save_result=-61;
 std::vector<CnnIteration> iterations;
};
class CnnSession {
 public:
 CnnSession();explicit CnnSession(CnnIo);
 int open();int call(uint32_t,vxl_request *);int close();
 int setup(const CnnWeights &,const int32_t[64],vxl_request *);
 int cleanup(CnnRun *);int copy(unsigned,void *,bool,uint64_t *confirmed=nullptr);
 const char *scope()const{return injected_?"TEST_PROTOCOL_NOT_GPU":"REAL_DRIVER_CNN_I32_V1";}
 std::array<CnnObject,11> objects{};uint32_t jobs_before=0;
 private:
 CnnIo io_{};vxl_transport transport_{-1,nullptr};bool injected_=false,opened_=false,closed_=false;int close_result_=0;
};
int cnn_run_iteration(CnnSession &,const CnnWeights &,const int32_t[64],const CnnOutputs &,CnnNowFn,void *,CnnIteration *);
int cnn_run_suite(CnnSession &,const CnnWeights &,CnnRun *);
CnnWeights cnn_fixed_weights();
std::string cnn_iteration_json(const CnnIteration &,const char *scope);
int cnn_save_run(const std::string &,CnnRun *);
#endif
