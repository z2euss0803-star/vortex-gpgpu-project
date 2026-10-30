#pragma once
#include <vxnn/types.hpp>
#include <mutex>
namespace vxnn {
class EvidenceSink {public:virtual ~EvidenceSink()=default;virtual Result<void> emit(const Event&)=0;};
class FileEvidenceSink:public EvidenceSink {
 int borrowed_fd_;std::mutex mutex_;
public:
 explicit FileEvidenceSink(int fd):borrowed_fd_(fd){}
 Result<void> emit(const Event&) override;
};
struct TimingSummary {uint64_t exclusive_total_ns=0,wall_ns=0;};
Result<TimingSummary> summarize_timing(const std::vector<Event>&);
}
