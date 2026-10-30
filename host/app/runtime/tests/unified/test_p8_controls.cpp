#include <vxunified/p8.hpp>
#include <cstdio>
#include <cstdlib>
using namespace vxunified;
static void check(bool b,const char*s){if(!b){std::fprintf(stderr,"U16_ASSERT %s\n",s);std::exit(1);}}
static SceneState control(const SceneState&s,char c,uint64_t now=10){auto r=scene_control(s,c,now);check(bool(r),"frozen P8 control accepted");return *r.value;}
int main(){
 SceneState s;check(s.object==P8Object::PORCELAIN&&s.remaining==100&&s.startup_remaining==100,"original initial100 identity frames");
 auto blocked=scene_control(s,'3',0);check(!blocked&&blocked.error->code==EAGAIN,"startup controls must stay gated");
 s.startup_remaining=0;s.remaining=0;s.theme[0]=7;
 const char keys[8]={'f','q','a','z','b','c','d','e'};const uint32_t poses[8]={0,100,25,101,50,102,75,103};
 s=control(s,'3');check(s.object==P8Object::TERRACOTTA&&s.gallery&&!s.automatic&&s.pose==0&&s.remaining==1&&!s.looping&&s.theme[0]==7,"warrior default gallery preserves texture theme");
 for(unsigned i=0;i<8;i++){auto v=control(s,keys[i]);check(v.view==i&&v.pose==poses[i]&&v.remaining==1&&!v.paused&&v.gallery,"eight frozen45degree poses");}
 for(char c:{'r','l'}){auto r=scene_control(s,c,20);check(!r&&r.error->code==EOPNOTSUPP,"warrior continuous rotation forbidden");}
 s=control(s,'g',100);check(s.automatic&&s.view==0,"automatic starts front");s.remaining=0;s.gallery_due_ns=1000;auto early=scene_tick(s,999);check(bool(early)&&early.value->view==0,"gallery holds before deadline");auto next=scene_tick(s,1000);check(bool(next)&&next.value->view==1&&next.value->pose==100&&next.value->remaining==1,"warrior automatic advances one45degree view");
 s=control(s,'1');check(!s.gallery&&!s.looping&&s.remaining==100&&s.pose==0,"return low object resets original100");
 for(char c:{'q','e','z','c'}){auto r=scene_control(s,c,20);check(!r&&r.error->code==EOPNOTSUPP,"diagonal views restricted to warrior");}
 s=control(s,'l');check(s.looping&&!s.remaining&&!s.gallery&&s.pose==0,"low object loop");s=control(s,'p');check(s.paused,"pause");s=control(s,'p');check(!s.paused&&s.looping,"resume");s=control(s,'s');check(!s.looping&&!s.remaining&&!s.paused&&!s.gallery&&!s.automatic,"stop");s=control(s,'p');s=control(s,'p');check(s.looping,"resume stopped low object starts loop");
 s=control(s,'g');s.remaining=0;s.gallery_due_ns=100;auto low_next=scene_tick(s,100);check(bool(low_next)&&low_next.value->view==2&&low_next.value->pose==25,"low gallery advances90degrees");
 SceneState start;for(unsigned i=0;i<100;i++){auto frame=scene_after_frame(start,i,false,i+1);check(bool(frame),"initial frame progression");start=*frame.value;}check(!start.startup_remaining&&!start.remaining&&start.completed==100&&start.pose==0,"initial100 logical frames completed without GPU claim");
 start=control(start,'l');start.have_displayed=true;start.displayed_pose=99;auto redraw=scene_after_frame(start,99,true,200);check(bool(redraw)&&redraw.value->pose==start.pose&&redraw.value->remaining==start.remaining,"texture redraw preserves animation next pose");
 check(!scene_control(start,'?',0),"unknown command rejected");
 puts("U16_P8_FROZEN_CONTROL_STATE_PASS_HOST_NOT_GPU_DISPLAY gpu_raw_cycles=null");
}
