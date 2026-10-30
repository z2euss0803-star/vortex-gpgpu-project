#pragma once
#include <vxunified/daemon.hpp>
namespace vxunified {
// Combines metadata ingress into the existing sole worker and common owner.
// Does not create a queue, worker, display, backend or device descriptor.
Result<void>merge_native_ingress(ApplicationSet&,ApplicationSet);
}
