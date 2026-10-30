#include <vxunified/native_result_view.hpp>
#include <vxl/hunet.h>
#include "nnrt/package/json.hpp"
#include "scheduler_fixture.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
namespace {
std::vector<uint8_t>read(const std::string&p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen view file");return {std::istreambuf_iterator<char>(f),{}};}
BackendIdentity hardware(){BackendIdentity h;h.hardware_contract=digest(11);h.bit=digest(12);h.xsa=digest(13);h.compatible=digest(14);h.device_generation=1;h.provider_flags=1;return h;}
Digest hex(const std::string&s){auto j=vxnn::json::parse("\""+s+"\"");return vxnn::json::digest(j.get());}
struct Port:ExecutionPort {
 unsigned runs=0;Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(hardware());}
 Result<void>execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override{++runs;return Result<void>::failure({EPERM,"VIEW_TEST","no GPU execution permitted"});}
 Result<void>retire_request(int,RequestKey)override{return Result<void>::success();}
};
}
int main(int argc,char**argv){
 require(argc==3,"sealed view directory and manifest SHA");char root[]="/tmp/vx-native-files-XXXXXX";require(mkdtemp(root),"private shared result root");printf("NATIVE_FILES_OWNER=%s\n",root);fflush(stdout);
 ServiceConfig config;config.private_dir=root;auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"HOST owner");auto port=std::make_shared<Port>();auto rt=value(Runtime::start(owner,hardware(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"shared Runtime");
 StartupContext ctx;ctx.runtime=rt;ctx.results=rt->trusted_results();ctx.identity=hardware();ctx.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;ctx.boot_id="boot";ctx.private_dir=root;
 auto directory=std::string(root)+"/outputs";require(!mkdir(directory.c_str(),0700),"sole result directory");auto files=value(NativeFiles::create(ctx,directory),"sole shared file budget");const auto before=rt->stats().cpu_bytes;
 NativeHunetViewSpec spec;spec.directory=argv[1];spec.manifest=hex(argv[2]);spec.models={digest(20)};
 auto created=NativeResultView::create(ctx,files,spec);if(!created)std::fprintf(stderr,"NATIVE_VIEW_CREATE_ERROR code=%d category=%s detail=%s\n",created.error->code,created.error->category.c_str(),created.error->detail.c_str());require(bool(created),"U20 native HUNet result must bind sealed original source/labels/glyphs in shared CPU budget");auto view=*created.value;require(rt->stats().cpu_bytes>before,"view payload lifetime charged before reading assets");
 auto bad=spec;bad.manifest[0]^=1;const auto charged=rt->stats().cpu_bytes;require(!NativeResultView::create(ctx,files,bad)&&rt->stats().cpu_bytes==charged,"manifest refusal rolls back complete host memory charge");
 auto foreign=ctx;foreign.identity.bit[0]^=1;require(!NativeResultView::create(foreign,files,spec),"foreign Runtime identity cannot lend view storage");foreign=ctx;foreign.scope=vxnn::EvidenceScope::REAL_BOARD;require(!NativeResultView::create(foreign,files,spec),"HOST cannot become board display evidence");
 const RequestKey key{"boot","view","roi"};auto logits=read("tests/demo/fixtures/hunet/logits.f32");auto writer=value(files->request(key,4096,logits.size()),"checked result storage");auto artifact=value(writer->output("logits.f32",{logits.data(),logits.size()},"CHW_F32",{8105,1,1}),"immutable historical-reference logits fixture");
 PublishedResult p;p.execution_complete=true;p.lease.producer=key;p.lease.app=AppKind::NN;p.lease.scope=ctx.scope;p.lease.hardware=ctx.identity.hardware_contract;p.lease.model=digest(20);p.lease.plan=digest(21);p.lease.artifacts={artifact};p.raw_legacy_json="{\"source\":\"FROZEN_OFFLINE_REFERENCE_NOT_NEW_INFERENCE\"}";
 ExecutionBinding b;b.app=AppKind::NN;b.hardware=ctx.identity.hardware_contract;b.bit=ctx.identity.bit;b.xsa=ctx.identity.xsa;b.compatible=ctx.identity.compatible;b.model=p.lease.model;b.plan=p.lease.plan;b.input=hex("de5513346d7922032e415b2d5eb40a5862b084a2ed264b73602ab8f00769f0de");b.source=digest(22);b.input_bytes=196608;b.device_generation=1;b.provider_flags=1;p.binding=b;
 auto layout=value(view->layout(p,1280,720),"dedicated original HUNet panel");require(layout.source_width==474&&layout.source_height==345&&layout.viewport_width==960&&layout.viewport_height==720,"source ROI dimensions and original three-quarter viewport");
 const auto compose_before=rt->stats().cpu_bytes;auto page=value(view->compose(p,layout),"original top5/ROI page from retained result");require(page->source_artifact==artifact.sha&&page->rgb.size()==1280u*720u*3u,"exact retained logits and page extent");
 require(rt->stats().cpu_bytes>=compose_before+page->rgb.size(),"retained view canvas owns a real shared CPU lease");
 auto labels_bytes=read(spec.directory+"/labels.bin"),atlas=read(spec.directory+"/glyphs-u8.bin"),rgb=read(spec.directory+"/source.rgb");demo_labels labels{};require(!demo_labels_bind(labels_bytes.data(),labels_bytes.size(),atlas.data(),atlas.size(),"ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f","763146584cf0710223441356b4395e279021b0806c196614377a7a0174ae074a",&labels),"independent original decoder assets");labels.bbox={40,12,87,105,474,345,0};
 std::vector<float>numbers(8105);std::memcpy(numbers.data(),logits.data(),logits.size());demo_hunet_result expected{};require(!demo_hunet_decode(numbers.data(),numbers.size(),&labels,&expected)&&expected.top[0].class_id==379&&!std::strcmp(expected.top[0].utf8,"臣"),"frozen historical top1 independent expected value");
 demo_frame source{};source.width=474;source.height=345;source.stride=1422;source.bytes=rgb.size();source.pixels=rgb.data();require(!demo_frame_hash(&source),"source reference pixels");expected.source_frame=&source;expected.view_scope=DEMO_VIEW_OFFLINE;expected.numeric_class=DEMO_NUMERIC_UNEVALUATED;std::strcpy(expected.run_id,"d07ca27414f77cc3b0736847f1cad7ba");demo_sha_hex(p.raw_legacy_json.data(),p.raw_legacy_json.size(),expected.source_result_sha);
 demo_frame original{};require(!demo_frame_from_hunet(&expected,&original),"frozen original compositor");require(page->rgb.size()==original.bytes&&!std::memcmp(page->rgb.data(),original.pixels,original.bytes),"native view preserves original visual output without inference/accuracy promotion");demo_frame_free(&original);demo_labels_close(&labels);
 auto wrong=p;wrong.binding->input[0]^=1;require(!view->layout(wrong,1280,720),"source image cannot be borrowed for another input");wrong=p;wrong.lease.model[0]^=1;wrong.binding->model=wrong.lease.model;require(!view->layout(wrong,1280,720),"foreign model cannot borrow HUNet labels");wrong=p;wrong.numeric=vxnn::NumericClass::PASS;require(!view->compose(wrong,layout),"numeric PASS requires its actual evaluation evidence, never a display guess");
 page.reset();require(rt->stats().cpu_bytes==compose_before,"last canvas retirement releases its complete shared charge");
 // New input preview must come from this request's actual sealed tensor,
 // never from the installed frozen source.rgb. These are HOST-only logits.
 std::vector<float>processed(3*128*128);for(size_t i=0;i<128*128;i++){processed[i]=-1.f;processed[128*128+i]=0.f;processed[2*128*128+i]=1.f;}
 auto make_dynamic=[&](const std::string&id,const std::string&policy){
  auto q=p;q.lease.producer={"boot","view",id};q.binding->input=vxnn::content_digest(std::string(reinterpret_cast<const char*>(processed.data()),processed.size()*4));
  auto own=value(files->request(q.lease.producer,4096,logits.size()+processed.size()*4+4096),"new request input/output quota");
  auto output=value(own->output("logits.f32",{logits.data(),logits.size()},"CHW_F32",{8105,1,1}),"same request logits");
  auto tensor=value(own->output("request-input.f32",{reinterpret_cast<const uint8_t*>(processed.data()),processed.size()*4},"CHW_F32",{3,128,128}),"authoritative processed tensor");
  std::ostringstream metadata;metadata<<"{\"format\":\"nn-input-record-v1\",\"kind\":\"UPLOADED_IMAGE_TENSOR\",\"model\":\""<<vxnn::digest_hex(q.lease.model)<<"\",\"plan\":\""<<vxnn::digest_hex(q.lease.plan)<<"\",\"tensor_sha\":\""<<vxnn::digest_hex(q.binding->input)<<"\",\"bytes\":196608,\"source_sha\":\""<<vxnn::digest_hex(digest(31))<<"\",\"source_format\":\"PNG\",\"preprocessing\":\""<<policy<<"\",\"preprocessing_id\":\""<<vxnn::digest_hex(vxnn::content_digest(policy))<<"\",\"dtype\":\"FP32\",\"layout\":\"CHW_F32\",\"shape\":[3,128,128],\"input_artifact\":\"request-input.f32\",\"gpu_raw_cycles\":null,\"numeric\":\"NOT_EVALUATED\"}";
  auto text=metadata.str();auto record=value(own->output("input-record.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()},"JSON_UTF8",{text.size()}),"authoritative request provenance");q.lease.artifacts={output,record,tensor};q.raw_legacy_json="{\"source\":\"HOST_DYNAMIC_INPUT_PREVIEW_NOT_NEW_INFERENCE\"}";return q;
 };
 auto dynamic=make_dynamic("processed","HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1");auto dynamic_layout=value(view->layout(dynamic,1280,720),"new HUNet input must use authoritative processed preview");
 require(dynamic_layout.source_width==128&&dynamic_layout.source_height==128,"processed tensor dimensions replace frozen source dimensions");
 auto dynamic_page=value(view->compose(dynamic,dynamic_layout),"request tensor preview with fixed original labels");
 const size_t center=(360u*1280u+480u)*3u;require(dynamic_page->rgb[center]==0&&dynamic_page->rgb[center+1]==128&&dynamic_page->rgb[center+2]==255,"RGB preview derives from actual normalized CHW tensor");
 auto invalid=dynamic;invalid.lease.artifacts.pop_back();require(!view->layout(invalid,1280,720),"new input cannot display without actual input artifact");
 invalid=dynamic;invalid.lease.producer.request_id="borrowed";require(!view->layout(invalid,1280,720),"all artifacts together cannot impersonate another producer request");
 invalid=dynamic;invalid.lease.artifacts.back().sha[0]^=1;require(!view->compose(invalid,dynamic_layout),"tampered tensor identity refused before display");
 auto hostile=make_dynamic("badpolicy","UNDECLARED_PREPROCESSING");require(!view->layout(hostile,1280,720),"unapproved preprocessing cannot borrow HUNet preview");
 auto another=make_dynamic("another","HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1");invalid=dynamic;invalid.lease.artifacts.back()=another.lease.artifacts.back();require(!view->layout(invalid,1280,720),"another request cannot lend its tensor even if SHA matches");
 auto frozen_tensor=read("data/demo/hunet-input-roi0.f32");require(frozen_tensor.size()==processed.size()*4,"original frozen tensor extent");std::memcpy(processed.data(),frozen_tensor.data(),frozen_tensor.size());
 auto same_tensor=make_dynamic("sameprocessed","HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1");require(same_tensor.binding->input==b.input&&value(view->layout(same_tensor,1280,720),"uploaded provenance with frozen-equivalent tensor").source_width==128,"same processed SHA must not borrow another original source image");
 dynamic_page.reset();
 require(!port->runs&&!p.gpu_raw_cycles&&p.numeric==vxnn::NumericClass::NOT_EVALUATED,"display adapter never dispatches or upgrades source evidence");ok(rt->shutdown(),"empty compute runtime closes");puts("U20_NATIVE_HUNET_SOURCE_TOP5_ROI_ORIGINAL_PIXELS_SHARED_BUDGET_PASS_HOST_REFERENCE_ONLY_NOT_GPU_INFERENCE_DP gpu_raw_cycles=null");
}
