#pragma once
#include <vxunified/native_media.hpp>
#include <vxunified/texture_service.hpp>
namespace vxunified {
struct NativeP8ServiceStats {
 uint64_t packets=0,wire_bytes=0,protocol_ns=0,prepare_ns=0,complete_ns=0,completed=0;
 std::optional<uint64_t>gpu_raw_cycles;
};
struct NativeP8ServiceSnapshot {SceneState scene;NativeP8ServiceStats timing;bool active=false;uint32_t startup_selftest_frames=100;};
// One scene owner on the existing daemon worker; no second execution queue.
// Trusted installation selects daily1 or independent maintenance100. Existing
// callers retain the original100-frame default; no runtime counter override.
class NativeP8Service:public std::enable_shared_from_this<NativeP8Service> {
 struct State;std::shared_ptr<State>state_;explicit NativeP8Service(std::shared_ptr<State>);
public:
 ~NativeP8Service();
 static Result<std::shared_ptr<NativeP8Service>>create(const StartupContext&,std::shared_ptr<NativeP8Library>,std::shared_ptr<NativeRouter>,uint32_t session,uint64_t display_base,uint16_t port=5007,const std::string&address="0.0.0.0",uint32_t startup_selftest_frames=100);
 Result<void>bind(ApplicationSet&,std::shared_ptr<NativeFiles> common_files={},std::vector<RegisteredTextureFile> textures={});
 Result<void>poll(uint32_t timeout_ms);Result<bool>step();
 // Cleanup only: no scene tick, frame preparation, admission or GPU dispatch.
 Result<void>settle_inactive();
 Result<void>control(char command);
 Result<void>stage_file(std::shared_ptr<const TexturePackage>,std::shared_ptr<HostMemoryLease>);
 // Compiled joint bridge only: the caller binds this spec to the original
 // completed Mosaic export; RPC carries a producer key, never a path.
 Result<std::string>accept_mosaic_file(const RegisteredTextureFile&,const RequestKey&producer);
 Result<std::string>mosaic_file_status(const RequestKey&producer)const;
 Result<NativeP8ServiceSnapshot>snapshot()const;
 Result<void>quiesce();Result<void>release();uint16_t port()const;
};
}
