#include <vxl/demo_frame.h>
#include <json-c/json.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <cstdio>
static std::vector<uint8_t> read(const char *p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(int argc,char **argv){
 assert(argc==2);std::string directory=argv[1];
 auto table=read("data/demo/hunet-labels.bin"),atlas=read("data/demo/hunet-glyphs-u8.bin"),logits_bytes=read("tests/demo/fixtures/hunet/logits.f32"),pixels=read("tests/demo/fixtures/hunet/display-source.rgb"),meta=read("tests/demo/fixtures/hunet/display-source.json");
 json_object *metadata=json_tokener_parse(std::string(meta.begin(),meta.end()).c_str());assert(metadata);
 demo_labels labels{};demo_hunet_result result{};demo_frame source{},canvas{};
 assert(!demo_labels_bind(table.data(),table.size(),atlas.data(),atlas.size(),"ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f","763146584cf0710223441356b4395e279021b0806c196614377a7a0174ae074a",&labels));
 auto *size=json_object_object_get(metadata,"original_size"),*bbox=json_object_object_get(metadata,"bbox");
 labels.bbox={json_object_get_int(json_object_array_get_idx(bbox,0)),json_object_get_int(json_object_array_get_idx(bbox,1)),json_object_get_int(json_object_array_get_idx(bbox,2)),json_object_get_int(json_object_array_get_idx(bbox,3)),uint32_t(json_object_get_int(json_object_array_get_idx(size,0))),uint32_t(json_object_get_int(json_object_array_get_idx(size,1))),0};
 source.width=uint32_t(json_object_get_int(json_object_object_get(metadata,"width")));source.height=uint32_t(json_object_get_int(json_object_object_get(metadata,"height")));source.stride=source.width*3;source.bytes=pixels.size();source.pixels=pixels.data();assert(!demo_frame_check(&source,0));
 demo_sha_hex(pixels.data(),pixels.size(),source.raw_source_sha);assert(!strcmp(source.raw_source_sha,json_object_get_string(json_object_object_get(metadata,"sha256"))));
 std::vector<float> logits(DEMO_HUNET_CLASSES);assert(logits_bytes.size()==logits.size()*4);memcpy(logits.data(),logits_bytes.data(),logits_bytes.size());
 assert(!demo_hunet_decode(logits.data(),logits.size(),&labels,&result));
 assert(!demo_hunet_classify(logits.data(),logits.data(),logits.size(),0,1,&result));
 result.source_frame=&source;result.view_scope=DEMO_VIEW_HISTORICAL;
 strcpy(result.run_id,"39fb432d2a3549a5abdf8644ca6c2d6e");strcpy(result.source_result_sha,json_object_get_string(json_object_object_get(metadata,"source_result_sha256")));
 assert(!demo_frame_from_hunet(&result,&canvas));
 std::ofstream raw(directory+"/hunet-historical.rgb",std::ios::binary);raw.write(reinterpret_cast<const char *>(canvas.pixels),canvas.bytes);raw.close();assert(raw);
 json_object *out=json_object_new_object();
 json_object_object_add(out,"scope",json_object_new_string("OFFSCREEN_UNPUBLISHED_HISTORICAL_VIEW_NOT_DP_NOT_INFERENCE"));
 json_object_object_add(out,"source_result_kind",json_object_new_string("LEGACY_PROBE_AUDIT_NOT_DEMO_COMPUTE_RESULT"));
 json_object_object_add(out,"run_id",json_object_new_string(canvas.run_id));json_object_object_add(out,"source_result_sha",json_object_new_string(canvas.source_result_sha));
 json_object_object_add(out,"width",json_object_new_int(canvas.width));json_object_object_add(out,"height",json_object_new_int(canvas.height));json_object_object_add(out,"stride",json_object_new_int(canvas.stride));json_object_object_add(out,"bytes",json_object_new_uint64(canvas.bytes));json_object_object_add(out,"sha256",json_object_new_string(canvas.sha256));json_object_object_add(out,"raw_logits_sha256",json_object_new_string(result.logits_sha));json_object_object_add(out,"raw_gpu_cycles",nullptr);
 std::ofstream report(directory+"/frame.json");report<<json_object_to_json_string_ext(out,JSON_C_TO_STRING_PRETTY)<<"\n";report.close();assert(report);
 puts(json_object_to_json_string_ext(out,JSON_C_TO_STRING_PLAIN));json_object_put(out);json_object_put(metadata);demo_labels_close(&labels);demo_frame_free(&canvas);return 0;
}
