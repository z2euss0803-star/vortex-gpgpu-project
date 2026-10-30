#include <vxunified/ingress_mux.hpp>
#include <set>
#include <cstdio>
namespace vxunified {
Result<void>merge_native_ingress(ApplicationSet&base,ApplicationSet next){
 auto cleanup=[&](){try{if(next.quiesce){auto r=next.quiesce();if(!r)return;}if(next.release_resources){auto r=next.release_resources();if(!r)std::fprintf(stderr,"INGRESS_MUX_RELEASE_ERROR %d\n",r.error->code);}}catch(...){std::fprintf(stderr,"INGRESS_MUX_RELEASE_EXCEPTION\n");}};
 try{
  if(!base.poll_native||!base.prepare_native||!next.poll_native||!next.prepare_native||next.display||next.retire_display||next.installation_sha){cleanup();return Result<void>::failure({EINVAL,"INGRESS_MUX","two compiled ingress hooks and one existing display/installation required"});}
  std::set<std::string>ids;for(const auto&a:base.entries)ids.insert(a.id);
  for(const auto&a:next.entries)if(a.id.empty()||!ids.insert(a.id).second){cleanup();return Result<void>::failure({EEXIST,"INGRESS_MUX","duplicate application entry"});}
  if(ids.size()>64){cleanup();return Result<void>::failure({ENOSPC,"INGRESS_MUX","common registry limit64"});}
  ApplicationSet merged=base;merged.entries.insert(merged.entries.end(),next.entries.begin(),next.entries.end());
  auto old_poll=base.poll_native,new_poll=next.poll_native;
  merged.poll_native=[old_poll,new_poll](uint32_t timeout){auto a=old_poll(std::min(timeout,25u));if(!a)return a;return new_poll(0);};
  auto old_prepare=base.prepare_native,new_prepare=next.prepare_native;auto turn=std::make_shared<bool>(false);
  merged.prepare_native=[old_prepare,new_prepare,turn](){bool right=*turn;*turn=!right;auto first=right?new_prepare():old_prepare();if(!first||*first.value)return first;return right?old_prepare():new_prepare();};
  auto old_settle=base.settle_native,new_settle=next.settle_native;
  merged.settle_native=[old_settle,new_settle](){if(old_settle){auto r=old_settle();if(!r)return r;}return new_settle?new_settle():Result<void>::success();};
  auto old_activate=base.activate,new_activate=next.activate;
  merged.activate=[old_activate,new_activate](){if(old_activate){auto r=old_activate();if(!r)return r;}return new_activate?new_activate():Result<void>::success();};
  auto old_stop=base.quiesce,new_stop=next.quiesce;
  merged.quiesce=[old_stop,new_stop](){if(new_stop){auto r=new_stop();if(!r)return r;}return old_stop?old_stop():Result<void>::success();};
  auto old_release=base.release_resources,new_release=next.release_resources;
  merged.release_resources=[old_release,new_release](){if(new_release){auto r=new_release();if(!r)return r;}return old_release?old_release():Result<void>::success();};
  if(next.export_result)merged.export_result=next.export_result;
  base=std::move(merged);return Result<void>::success();
 }catch(const std::bad_alloc&){cleanup();return Result<void>::failure({ENOMEM,"INGRESS_MUX","bounded shared-hook allocation"});}
}
}
