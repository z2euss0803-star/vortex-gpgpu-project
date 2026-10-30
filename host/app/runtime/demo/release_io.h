#ifndef DEMO_RELEASE_IO_H
#define DEMO_RELEASE_IO_H
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <vector>
/* Production caller uses uid0. Host tests use their own uid in private trees.
 * NULL expected SHA is only for fixed-size bindings verified by their caller. */
int demo_release_open_root(const char *,uid_t);
int demo_release_read(int,const char *,size_t,const uint8_t *,uid_t,std::vector<uint8_t>&);
#endif
