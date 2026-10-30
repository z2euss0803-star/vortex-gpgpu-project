#include <vxl/demo_frame.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
static int hex(const char *s,size_t n){size_t i;if(strlen(s)!=n)return 0;for(i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
static void red(struct demo_frame *f,uint32_t x,uint32_t y){uint8_t *p=f->pixels+(size_t)y*f->stride+3*x;p[0]=255;p[1]=p[2]=0;}
int demo_frame_from_hunet(const struct demo_hunet_result *r,struct demo_frame *frame){
 struct demo_rect fit,box,region;uint32_t panel,font_scale,big,row,top;int rc;char text[64];const char *numeric,*scope;
 if(!r||!frame||!r->completed||!r->source_frame||demo_frame_check(r->source_frame,0)||demo_frame_overlap(r->source_frame,frame))return -ENODATA;
 if(!hex(r->run_id,32)||!hex(r->source_result_sha,64)||!hex(r->logits_sha,64))return -EINVAL;
 if(r->numeric_class==DEMO_NUMERIC_PASS&&(!r->ort_evaluated||!r->ort_pass||!r->r2_evaluated||!r->r2_pass))return -EINVAL;
 if(r->numeric_class==DEMO_NUMERIC_KNOWN_DEVIATION&&!r->history_byte_equal)return -EINVAL;
 if(r->view_scope==DEMO_VIEW_HISTORICAL)scope="HISTORICAL";
 else if(r->view_scope==DEMO_VIEW_OFFLINE)scope="OFFLINE";
 else if(r->view_scope==DEMO_VIEW_LIVE)scope="LIVE";else return -EINVAL;
 if(!frame->pixels){rc=demo_frame_alloc(1280,720,0,frame);if(rc)return rc;}
 if(demo_frame_check(frame,1)||frame->width<640||frame->height<480)return -ERANGE;
 for(uint32_t y=0;y<frame->height;y++)for(uint32_t x=0;x<frame->width;x++){uint8_t *p=frame->pixels+(size_t)y*frame->stride+3*x;p[0]=18;p[1]=21;p[2]=26;}
 panel=frame->width*3/4;region=(struct demo_rect){0,0,panel,frame->height};
 rc=demo_letterbox(r->source_frame,frame,region,&fit);if(rc)return rc;
 rc=demo_map_bbox(&r->bbox,fit,&box);if(rc)return rc;
 for(uint32_t x=box.x;x<box.x+box.width;x++){red(frame,x,box.y);red(frame,x,box.y+box.height-1);}
 for(uint32_t y=box.y;y<box.y+box.height;y++){red(frame,box.x,y);red(frame,box.x+box.width-1,y);}
 font_scale=frame->height>=600?2:1;big=font_scale*2;
 rc=demo_draw_ascii(frame,"HUNET",panel+16,12,font_scale,0xffffff);if(rc)return rc;
 rc=demo_draw_ascii(frame,scope,panel+16,12+18*font_scale,1,0x87bfff);if(rc)return rc;
 numeric=r->numeric_class==DEMO_NUMERIC_KNOWN_DEVIATION?"KNOWN DEVIATION":r->numeric_class==DEMO_NUMERIC_PASS?"NUMERIC PASS":r->numeric_class==DEMO_NUMERIC_FAIL?"NUMERIC FAIL":"NOT EVALUATED";
 rc=demo_draw_ascii(frame,numeric,panel+16,36+18*font_scale,1,0xffbd60);if(rc)return rc;
 snprintf(text,sizeof(text),"ORT %s / R2 %s",r->ort_evaluated?(r->ort_pass?"PASS":"FAIL"):"N/A",r->r2_evaluated?(r->r2_pass?"PASS":"FAIL"):"N/A");
 rc=demo_draw_ascii(frame,text,panel+16,56+18*font_scale,1,0xff8080);if(rc)return rc;
 rc=demo_draw_glyph(frame,&r->glyphs[0],panel+(frame->width-panel-32*big)/2,110,big);if(rc)return rc;
 top=frame->height>=600?280:210;row=(frame->height-top-16)/5;
 for(unsigned i=0;i<5;i++){
  if(!isfinite(r->top[i].score)||r->top[i].class_id>=DEMO_HUNET_CLASSES)return -EINVAL;
  rc=demo_draw_glyph(frame,&r->glyphs[i],panel+16,top+row*i,1);if(rc)return rc;
  snprintf(text,sizeof(text),"#%u",r->top[i].class_id);
  rc=demo_draw_ascii(frame,text,panel+56,top+row*i+4,1,0xeeeeee);if(rc)return rc;
  snprintf(text,sizeof(text),"%.4f",r->top[i].score);
  rc=demo_draw_ascii(frame,text,panel+56,top+row*i+22,1,0xeeeeee);if(rc)return rc;
 }
 memcpy(frame->run_id,r->run_id,sizeof(frame->run_id));memcpy(frame->source_result_sha,r->source_result_sha,sizeof(frame->source_result_sha));memcpy(frame->raw_source_sha,r->logits_sha,sizeof(frame->raw_source_sha));
 memcpy(frame->input_source_sha,r->source_frame->raw_source_sha,sizeof(frame->input_source_sha));frame->layout=DEMO_LAYOUT_RESULT;return demo_frame_hash(frame);
}
