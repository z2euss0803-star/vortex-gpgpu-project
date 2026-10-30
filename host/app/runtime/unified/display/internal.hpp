#pragma once
#include <vxunified/display.hpp>
#include <vxunified/exit_retention.hpp>
#include <mutex>
namespace vxunified {namespace display_detail {
// Only the synchronous trusted display callback selects its charged cache.
// Standalone legacy composition remains stateless.
struct RasterCacheScope {
 RasterComposeCache*previous;
 explicit RasterCacheScope(RasterComposeCache*);~RasterCacheScope();
};
RasterComposeCache*active_raster_cache();
struct Hold {
 detail::ExitRetention exit;
 std::shared_ptr<ResultStore> store;
 const Result<ConsumerLease> lease;
 Hold(std::shared_ptr<ResultStore>s,const RequestKey&k):store(std::move(s)),lease(store->retain_result(k,Consumer::DP)){}
};
bool same_source(const ResultLease&,const ResultLease&);
Result<void> release(std::shared_ptr<Hold>&);
Result<demo_frame> frame(const PublishedResult&,const DisplayCanvas&,const DisplayLayout&,uint32_t,uint32_t);
}}
