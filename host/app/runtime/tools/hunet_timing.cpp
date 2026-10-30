#include "hunet_timing.h"
#include <time.h>
#include <stdexcept>
uint64_t hunet_now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)throw std::runtime_error("CLOCK_MONOTONIC");return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);}
uint64_t hunet_duration(uint64_t begin,uint64_t end){if(end<begin)throw std::runtime_error("TIME_REVERSED");return end-begin;}
