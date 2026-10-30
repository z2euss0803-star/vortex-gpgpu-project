#include <vxl/demo_release.h>
#include <vxl/demo_mosaic.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <cstdarg>
#include <memory>
static unsigned device_opens;
extern "C" int __real_open(const char *,int,...);
extern "C" int __wrap_open(const char *p,int flags,...){
 if(!strcmp(p,"/dev/vortex0")){++device_opens;errno=EPERM;return -1;}
 /* Host-only fixed build-id file fixture; production reads codex-build-id. */
 if(!strcmp(p,"/etc/codex-build-id"))return __real_open("/etc/hostname",flags);
 if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode_t mode=va_arg(ap,int);va_end(ap);return __real_open(p,flags,mode);}
 return __real_open(p,flags);
}
int main(int argc,char **argv){
 (void)argc;(void)argv;
 auto s=std::unique_ptr<demo_session>(new demo_session{});demo_store store{};store.dir_fd=-1;
 assert(demo_release_install(nullptr,&store,"/missing")==-EINVAL);
 assert(demo_release_install(s.get(),nullptr,"/missing")==-EINVAL);
 assert(demo_release_install(s.get(),&store,nullptr)==-EINVAL);
 assert(demo_release_install(s.get(),&store,"relative")==-EINVAL);
#ifndef DEMO_PROVIDER_NEGATIVE_ONLY
 if(argc==3){
  assert(!demo_store_open(&store,argv[2],nullptr));
  assert(demo_release_install(s.get(),&store,"/missing-codex-release-20261020")==-ENOENT);
  int rc=demo_release_install(s.get(),&store,argv[1]);fprintf(stderr,"PROVIDER_INSTALL_RESULT=%d DEVICE_OPENS=%u\n",rc,device_opens);assert(!rc);
  assert(s->dispatcher&&s->owner_fd==-1&&!device_opens);
  assert(s->identity_bound&&s->boot_id[0]&&s->build_id[0]);
  auto *m=static_cast<demo_mosaic_adapter*>(s->dispatcher->apps[0].context);
  assert(m->frame_bytes==1280u*960u*12u&&m->program->layouts[m->layout].layout.width==1280&&m->program->layouts[m->layout].layout.height==960);
  assert(demo_release_install(s.get(),&store,argv[1])==-EBUSY);
  assert(!demo_release_uninstall(s.get()));assert(!s->dispatcher&&!s->release_provider_close);
  assert(!demo_release_uninstall(s.get()));demo_store_close(&store);
 }
#endif
 assert(!device_opens);puts("RELEASE_PROVIDER_ADMISSION_NO_DEVICE_OPEN_NOT_GPU");
}
