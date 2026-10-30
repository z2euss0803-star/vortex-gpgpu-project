#pragma once
#include <vxunified/daemon.hpp>
namespace vxunified {
Result<DaemonConfig> decode_daemon_config(const std::string&bytes,const std::string&kernel_boot_id);
// Compiled provider registry; no plugin path, callback or descriptor from RPC.
ApplicationBuilder installed_applications();
}
