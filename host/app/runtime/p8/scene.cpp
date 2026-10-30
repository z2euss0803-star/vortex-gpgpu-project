#include <vxunified/p8.hpp>
#include <limits>
namespace vxunified {
namespace {
constexpr uint32_t poses[8]={0,100,25,101,50,102,75,103};
constexpr uint64_t hold_ns=4000000000ULL;
bool warrior(const SceneState&s){return s.object==P8Object::TERRACOTTA;}
bool valid(const SceneState&s){
 if(warrior(s)&&(std::find(std::begin(poses),std::end(poses),s.pose)==std::end(poses)||s.looping||s.remaining>1||s.startup_remaining))return false;
 if(s.startup_remaining&&s.object!=P8Object::PORCELAIN)return false;
 return uint32_t(s.object)<=2&&s.view<8&&s.pose<104&&s.displayed_pose<104&&
  s.remaining<=100&&s.startup_remaining<=100&&s.revision&&
  (warrior(s)||s.pose<100)&&(!s.automatic||s.gallery)&&(!s.gallery||!s.looping);
}
Result<SceneState>bad(int code,const char*why){return Result<SceneState>::failure({code,"P8_SCENE",why});}
Result<SceneState>changed(SceneState s){
 if(s.revision==std::numeric_limits<uint64_t>::max())return bad(EOVERFLOW,"scene revision exhausted");
 ++s.revision;return Result<SceneState>::success(s);
}
bool deadline(uint64_t now){return now<=std::numeric_limits<uint64_t>::max()-hold_ns;}
}
Result<SceneState>scene_control(const SceneState&source,char c,uint64_t now){
 if(!valid(source))return bad(EINVAL,"invalid scene");
 if(source.startup_remaining)return bad(EAGAIN,"initial100 identity gate incomplete");
 SceneState s=source;
 if(c=='i')return Result<SceneState>::success(s);
 if(c=='s'){s.gallery=s.automatic=s.looping=s.paused=false;s.remaining=0;}
 else if(c=='p'){
  s.paused=!s.paused;if(warrior(s))s.gallery=true;
  if(s.gallery){if(!s.paused){if(!deadline(now))return bad(EOVERFLOW,"gallery deadline");s.gallery_due_ns=now+hold_ns;}}
  else if(!s.remaining&&!s.looping&&!s.paused)s.looping=true;
 }else if(c=='r'||c=='l'){
  if(warrior(s))return bad(EOPNOTSUPP,"native512 uses fixed views");
  s.gallery=s.automatic=s.paused=false;s.pose=0;s.remaining=c=='r'?100:0;s.looping=c=='l';
 }else if(c=='f'||c=='a'||c=='d'||c=='b'||c=='g'||c=='q'||c=='e'||c=='z'||c=='c'){
  if(!warrior(s)&&(c=='q'||c=='e'||c=='z'||c=='c'))return bad(EOPNOTSUPP,"diagonal views require warrior");
  s.gallery=true;s.automatic=c=='g';s.view=c=='a'?2:c=='b'?4:c=='d'?6:c=='q'?1:c=='z'?3:c=='c'?5:c=='e'?7:0;
  s.pose=poses[s.view];s.remaining=1;s.looping=s.paused=false;
 }else if(c>='1'&&c<='3'){
  const bool was_gallery=s.gallery;s.object=P8Object(c-'1');s.gallery=warrior(s);s.automatic=false;s.view=0;
  if(s.gallery||was_gallery)s.looping=false;
  s.pose=0;s.remaining=s.gallery?1:s.looping?0:100;s.paused=false;s.have_displayed=false;
 }else return bad(EINVAL,"unknown command");
 return changed(s);
}
Result<bool>scene_frame_due(const SceneState&s){
 if(!valid(s))return Result<bool>::failure({EINVAL,"P8_SCENE","invalid scene"});
 return Result<bool>::success(!s.paused&&(s.remaining||s.looping));
}
Result<SceneState>scene_render_state(const SceneState&source,bool redraw){
 auto due=scene_frame_due(source);if(!due)return Result<SceneState>::failure(*due.error);
 if(!redraw){if(!*due.value)return bad(EAGAIN,"normal frame not due");return Result<SceneState>::success(source);}
 if(source.startup_remaining||!source.have_displayed)return bad(EPROTO,"texture redraw requires completed displayed frame");
 auto checked=scene_after_frame(source,source.displayed_pose,true,0);if(!checked)return Result<SceneState>::failure(*checked.error);
 SceneState s=source;s.pose=source.displayed_pose;return Result<SceneState>::success(s);
}
Result<SceneState>scene_after_frame(const SceneState&source,uint32_t pose,bool redraw,uint64_t now){
 if(!valid(source))return bad(EINVAL,"invalid scene");
 if(pose>=104||(!warrior(source)&&pose>=100))return bad(EINVAL,"invalid rendered pose");
 if(redraw){
  if(source.startup_remaining||!source.have_displayed||pose!=source.displayed_pose)return bad(EPROTO,"texture redraw without matching displayed frame");
 }else if(source.paused||(!source.remaining&&!source.looping)||pose!=source.pose)return bad(EPROTO,"frame not due or pose changed");
 if(source.completed==std::numeric_limits<uint64_t>::max())return bad(EOVERFLOW,"frame count exhausted");
 SceneState s=source;s.displayed_pose=pose;s.have_displayed=true;++s.completed;
 if(!redraw){if(!s.gallery)s.pose=(s.pose+1)%100;if(s.remaining)--s.remaining;if(s.startup_remaining)--s.startup_remaining;}
 if(s.gallery&&!s.remaining){if(!deadline(now))return bad(EOVERFLOW,"gallery deadline");s.gallery_due_ns=now+hold_ns;}
 return changed(s);
}
Result<SceneState>scene_tick(const SceneState&source,uint64_t now){
 if(!valid(source))return bad(EINVAL,"invalid scene");
 SceneState s=source;
 if(!s.startup_remaining&&s.gallery&&s.automatic&&!s.paused&&!s.remaining&&now>=s.gallery_due_ns){
  s.view=(s.view+(warrior(s)?1:2))%8;s.pose=poses[s.view];s.remaining=1;return changed(s);
 }
 return Result<SceneState>::success(s);
}
}
