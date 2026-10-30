#pragma once
#include <vxunified/image_request.hpp>
namespace vxunified {
struct StartupContext;
struct ImagePeer {uint32_t address=0;uint16_t port=0;};
struct ImageIngress {uint64_t serial=0;ImageRequest image;std::shared_ptr<PreparationLease>preparation;};
// Sole service owns this receiver. Client packets cannot supply a ResultLease,
// execution flag, physical cycle count or DisplayReceipt. No device fd is opened.
class ImageReceiver {
 struct Impl;std::shared_ptr<Impl>impl_;explicit ImageReceiver(std::shared_ptr<Impl>);
 static Result<std::shared_ptr<ImageReceiver>>create_bound(const BackendIdentity&,const std::string&,uint32_t,const std::string&,uint64_t,vxnn::EvidenceScope,const StartupContext*);
public:
 ~ImageReceiver();
 // Two full frames plus a conservative64KiB bound for guards, static
 // bitmap/CRC/metrics and fixed receiver/boot metadata. Snapshot is per-request.
 static constexpr uint64_t persistent_bytes=uint64_t(1280)*720*4*2+65536;
 static Result<std::shared_ptr<ImageReceiver>>create(const BackendIdentity&,const std::string&boot,uint32_t session,const std::string&bundle,uint64_t display_base,vxnn::EvidenceScope);
 // Native I/O admission verifies the sole live owner and preserves the actual
 // measurement tier; it does not promote a configured bitstream to measured.
 static Result<std::shared_ptr<ImageReceiver>>create_native(const StartupContext&,uint32_t session,const std::string&bundle,uint64_t display_base);
 Result<std::vector<uint8_t>>handle(vxnn::Bytes,ImagePeer,uint64_t now_ms);
 Result<void>poll(uint64_t now_ms);
 Result<std::optional<ImageIngress>>acquire();
 // accepted is the exact key returned by the common Runtime::submit.
 Result<void>admitted(uint64_t serial,std::shared_ptr<ImageWorkRequest>,const RequestKey&accepted,uint32_t jobs_before);
 Result<void>complete(const ResultLease&,const DisplayReceipt&,uint64_t total_ns);
 Result<void>fail(uint64_t serial,uint32_t reason);
 Result<void>close();
};
}
