#pragma once
#include <vxunified/service.hpp>
namespace vxunified {
struct PreparationReceipt;
struct DisplayObservation;
struct IngressTiming {uint64_t serial=0,packets=0,bytes=0,protocol_ns=0,prepare_ns=0,complete_ns=0,begin_ns=0,end_ns=0;};
// Trusted private directory only; no RPC-provided filesystem destinations.
// Successful saves include file and directory synchronization. Failed partial
// files remain evidence; retry writes evidence, never repeats device work.
class EvidenceJournal {
 struct Impl;std::unique_ptr<Impl> impl_;
 explicit EvidenceJournal(std::unique_ptr<Impl>);
public:
 ~EvidenceJournal();EvidenceJournal(const EvidenceJournal&)=delete;
 static Result<std::shared_ptr<EvidenceJournal>> open(const std::string&private_dir);
 Result<void> save_startup(const BackendIdentity&,BackendMode,const std::string&boot,const Digest&config_sha,vxnn::EvidenceScope,const std::vector<std::string>&applications,std::optional<Digest>installation_sha={});
 Result<void> save_work(const ExecutionReceipt&,DeliveryStage);
 Result<void> save_result(const PublishedResult&);
 Result<void> save_result_stage(const PublishedResult&);
 Result<void> save_preparation(const PreparationReceipt&,vxnn::EvidenceScope);
 Result<void> save_ingress(const RequestKey&,const BackendIdentity&,vxnn::EvidenceScope,const IngressTiming&);
 // Normal frames/file redraws have no UDP packet activity. Distinct event kind,
 // not a relaxation of save_ingress or an invented packet count.
 Result<void> save_frame_timing(const RequestKey&,const BackendIdentity&,vxnn::EvidenceScope,const IngressTiming&);
 Result<void> save_display(const BackendIdentity&,vxnn::EvidenceScope,const DisplayObservation&);
};
}
