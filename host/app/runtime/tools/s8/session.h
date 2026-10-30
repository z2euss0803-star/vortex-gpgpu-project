#ifndef S8_SESSION_H
#define S8_SESSION_H
#include <cstddef>
#include <cstdint>
#include <string>
struct S8Options {std::string manifest,catalog,evidence,run_id;uint32_t case_id=0,core_mask=1,owner_warp=0,flags=0;};
int s8_parse_options(int,const char *const *,S8Options *);
int s8_persist(int,const char *,const void *,size_t);
int s8_session_run(const char *,const char *,const char *);
int s8_session_run_selected(const S8Options &);
#endif
