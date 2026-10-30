#include "display_fixture.hpp"
#include <fstream>
#include <iterator>
static std::vector<uint8_t> read(const char*name){std::ifstream f(name,std::ios::binary);need(bool(f),"frozen HUNet page asset");return {std::istreambuf_iterator<char>(f),{}};}
int main(){
 auto table=read("data/demo/hunet-labels.bin"),atlas=read("data/demo/hunet-glyphs-u8.bin"),raw=read("tests/demo/fixtures/hunet/logits.f32");
 demo_labels labels{};need(!demo_labels_bind(table.data(),table.size(),atlas.data(),atlas.size(),"ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f",nullptr,&labels),"original frozen classes and glyphs");labels.bbox={1,1,3,3,4,3,0};
 std::vector<float> logits(DEMO_HUNET_CLASSES);need(raw.size()==logits.size()*4,"exact logits extent");std::memcpy(logits.data(),raw.data(),raw.size());demo_hunet_result view{};need(!demo_hunet_decode(logits.data(),logits.size(),&labels,&view),"original top5 decoder");
 need(view.top[0].class_id==379&&!std::strcmp(view.top[0].utf8,"臣"),"frozen historical top1 independent expected value");need(!demo_hunet_classify(logits.data(),logits.data(),logits.size(),0,1,&view),"preserve original known ORT failure");
 demo_frame input{};need(!demo_frame_alloc(4,3,0,&input),"original image fixture");std::memset(input.pixels,255,input.bytes);need(!demo_frame_hash(&input),"original image digest");view.source_frame=&input;view.view_scope=DEMO_VIEW_HISTORICAL;std::strcpy(view.run_id,"11111111111111111111111111111111");
 auto p=producer();p.lease.app=AppKind::NN;p.lease.scope=vxnn::EvidenceScope::OFFLINE;p.lease.artifacts[0].layout="CHW_F32";p.lease.artifacts[0].shape={8105,1,1};p.lease.artifacts[0].bytes=raw.size();p.lease.artifacts[0].sha=sum(raw);demo_sha_hex(p.raw_legacy_json.data(),p.raw_legacy_json.size(),view.source_result_sha);
 DisplayLayout l{1280,720,4,3,0,0,960,720,PixelFormat::RGBA8888};auto original=view;
 auto page=get(compose_hunet_page(p,0,{raw.data(),raw.size()},view,l),"HUNet dedicated bound panel required");need(page->width==1280&&page->height==720&&page->source_artifact==sum(raw),"HUNet raw logits lineage");size_t box=(size_t(240)*1280+240)*3;need(page->rgb[box]==255&&page->rgb[box+1]==0&&page->rgb[box+2]==0,"original ROI maps to hand-derived red boundary240,240");
 demo_frame expected{};need(!demo_frame_from_hunet(&view,&expected),"frozen original UI compositor");need(page->rgb.size()==expected.bytes&&!std::memcmp(page->rgb.data(),expected.pixels,expected.bytes),"exact original HUNet visual and control state");need(!std::memcmp(&view,&original,sizeof(view))&&input.pixels[0]==255,"numeric/ORT/R2/input evidence untouched");
 auto changed=raw;changed[0]^=1;need(!compose_hunet_page(p,0,{changed.data(),changed.size()},view,l),"changed logits refused");auto wrong=view;wrong.top[0].class_id=1;need(!compose_hunet_page(p,0,{raw.data(),raw.size()},wrong,l),"top5 must bind actual logits");wrong=view;wrong.numeric_class=DEMO_NUMERIC_PASS;wrong.ort_pass=1;need(!compose_hunet_page(p,0,{raw.data(),raw.size()},wrong,l),"display cannot upgrade known numeric deviation");wrong=view;wrong.view_scope=DEMO_VIEW_LIVE;need(!compose_hunet_page(p,0,{raw.data(),raw.size()},wrong,l),"offline picture cannot claim LIVE");
 demo_frame_free(&expected);demo_frame_free(&input);demo_labels_close(&labels);puts("U18_HUNET_BOUND_TOP5_ROI_KNOWN_DEVIATION_ORIGINAL_PIXELS_PASS_HOST_NOT_INFERENCE_DP gpu_raw_cycles=null");return 0;
}
