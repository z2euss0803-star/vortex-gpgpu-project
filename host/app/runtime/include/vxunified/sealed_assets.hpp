#pragma once
#include <vxunified/types.hpp>
namespace vxunified {
// Trusted installation inputs: same-uid immutable regular file, bounded read,
// no symlink parents. Preserve the original installation error categories.
Result<std::string>read_sealed_native_asset(const std::string&path,size_t maximum);
}
