#pragma once
#include <vxunified/types.hpp>
#include <vxl/v2/objects.h>
#include <mutex>
#include <map>
namespace vxunified {
struct Lease {uint64_t token=0,owner=0,epoch=0;BufferRef buffer;Consumer consumer=Consumer::CPU;};
class ResourceTable {
 vxl_v2_ctx& context_;uint64_t owner_,epoch_;std::mutex& mutex_;std::map<uint64_t,Lease> leases_;
public:
 ResourceTable(vxl_v2_ctx& c,uint64_t owner,uint64_t epoch,std::mutex& device_mutex):context_(c),owner_(owner),epoch_(epoch),mutex_(device_mutex){}
 Result<Lease> acquire(BufferRef,Consumer);
 Result<void> release(Lease);
};
}
