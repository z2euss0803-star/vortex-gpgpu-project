#pragma once
#include <vxunified/service.hpp>
#include <vxunified/residency.hpp>
#include <vxnn/ops_int16.hpp>
namespace vxunified {
struct Int16NodeTrace {
 vxnn::NodeId node=0;bool complete=false;int error=0;
 uint64_t begin_ns=0,end_ns=0,program_ns=0,allocate_ns=0,seed_upload_ns=0,args_ns=0,guard_ns=0,download_ns=0,publish_ns=0,release_ns=0;
 std::optional<uint64_t>gpu_raw_cycles;std::optional<vxl_v2_completion>linux_detail;
};
struct Int16RequestReceipt {uint64_t completed=0;bool materialized=false,retired=false,published=false;std::optional<vxnn::Error>first_error,evidence_error;std::vector<Int16NodeTrace>nodes;};
using Int16TraceSink=std::function<Result<void>(const RequestKey&,const Int16NodeTrace&)>;
using Int16OutputSink=std::function<Result<PublishedResult>(const WorkRequest&,vxnn::Bytes,const Int16RequestReceipt&)>;
// Application graph/ops only, through the same native execution owner and v2 ABI.
class Int16Request final:public WorkSource,public std::enable_shared_from_this<Int16Request> {
 struct Impl;std::unique_ptr<Impl>impl_;explicit Int16Request(std::unique_ptr<Impl>);
public:
 ~Int16Request();
 static Result<std::shared_ptr<Int16Request>>prepare(vxnn::ModelPackage,const vxnn::Int16Family&,const DeviceCaps&,const BackendIdentity&,RequestKey,std::shared_ptr<ProgramResidency>,vxnn::EvidenceScope,uint32_t,uint64_t,Int16TraceSink,Int16OutputSink);
 uint64_t size()const override;Digest identity()const override;
 Result<WorkItem>get(uint64_t)const override;Result<bool>ready(uint64_t)const override;
 WorkRequest request()const;const vxnn::Plan&plan()const;ExecutionHooks hooks();
};
}
