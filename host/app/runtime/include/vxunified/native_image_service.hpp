#pragma once
#include <vxunified/native_media.hpp>
#include <vxunified/image_network.hpp>
namespace vxunified {
struct NativeImageServiceStats {
 uint64_t packets=0,wire_bytes=0,protocol_ns=0,prepare_ns=0,complete_ns=0,completed=0;
 std::optional<uint64_t>gpu_raw_cycles;
};
// Socket I/O is polled by the common control loop; preparation and completion
// run on the daemon's existing single worker, never another GPU execution loop.
class NativeImageService:public std::enable_shared_from_this<NativeImageService> {
 struct State;std::shared_ptr<State>state_;explicit NativeImageService(std::shared_ptr<State>);
public:
 ~NativeImageService();
 static Result<std::shared_ptr<NativeImageService>>create(const StartupContext&,std::shared_ptr<NativeImageLibrary>,uint32_t session,const std::string&bundle,uint64_t display_base,uint16_t port=5011,const std::string&address="0.0.0.0");
 Result<void>bind(ApplicationSet&);
 Result<void>poll(uint32_t timeout_ms);
 Result<bool>step();
 Result<void>quiesce();Result<void>release();
 uint16_t port()const;NativeImageServiceStats stats()const;
};
}
