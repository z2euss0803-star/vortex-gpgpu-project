#include <vxunified/image.hpp>
#include <vxl/hunet.h>
#include "generated/image/programs.h"
#include <cstring>
#include <mutex>
#include <new>
#include <sstream>
namespace vxunified {namespace {
vxnn::Error error(int code,const char*text){return {code,"IMAGE_ADAPTER",text};}
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
bool valid(const RequestKey&r){auto id=[](const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});};return id(r.boot_id)&&id(r.client_id)&&id(r.request_id);}
Digest hash(vxnn::Bytes bytes){Digest d{};vxl_hunet_hash(bytes.data,bytes.size,d.data());return d;}
bool hex_is(const Digest&d,const char*text){return vxnn::digest_hex(d)==text;}
bool opaque(vxnn::Bytes b){if(!b.data||b.size%4)return false;for(size_t i=3;i<b.size;i+=4)if(b.data[i]!=255)return false;return true;}
bool same_rect(const image_rect&a,const image_rect&b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}
bool display_valid(const DisplayReceipt&d,uint64_t cookie,vxnn::EvidenceScope scope){
 auto clock=scope==vxnn::EvidenceScope::REAL_BOARD?vxnn::ClockDomain::LINUX_MONOTONIC:vxnn::ClockDomain::HOST_MONOTONIC;
 return d.cookie==cookie&&d.clock_domain==clock&&d.submitted_ns&&d.presented_ns&&*d.presented_ns>=*d.submitted_ns&&(!d.had_old_front||(d.old_retired&&d.old_retired_ns&&*d.old_retired_ns>=*d.presented_ns));
}
}
uint32_t image_crc32(vxnn::Bytes bytes){
 // Thread-safe constant initialization; no mutable vendor lazy CRC table.
 static const std::array<uint32_t,256>table=[](){std::array<uint32_t,256>v{};for(unsigned i=0;i<256;i++){uint32_t c=i;for(unsigned j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);v[i]=c;}return v;}();
 uint32_t crc=~0u;for(size_t i=0;i<bytes.size;i++)crc=(crc>>8)^table[(crc^bytes.data[i])&255u];return ~crc;
}
struct ImageAdapter::Impl {
 struct Checkpoint {
  vx_image_state state{};vx_image_plan recipe{};ImageRequest request;ImageResult result;
  std::shared_ptr<const std::vector<uint8_t>>input;Digest input_sha{};
  std::array<Digest,33>output_hashes{};std::array<bool,33>output_valid{};bool initialized=false;
 };
 mutable std::mutex mutex;std::shared_ptr<const InstalledCatalog>catalog;DeviceCaps caps;Digest implementation{},reference{};
 vxnn::EvidenceScope scope;bool diagnostics=false,initialized=false;
 std::array<ProgramKey,10>programs;vx_image_state state{};vx_image_plan recipe{};
 ImageRequest request;ImageResult result;std::shared_ptr<const std::vector<uint8_t>>input;Digest input_sha{};
 std::array<Digest,33>output_hashes{};std::array<bool,33>output_valid{};std::optional<vxnn::Error>fault;uint64_t last_sequence=0;
 std::unique_ptr<Checkpoint>checkpoint;
 std::shared_ptr<ResidentLease>native_charge;
 vxnn::Error fail(int code,const char*text){if(!fault)fault=error(code,text);result.first_error=fault;return *fault;}
};
ImageAdapter::ImageAdapter(std::unique_ptr<Impl>p):impl_(std::move(p)){}ImageAdapter::~ImageAdapter()=default;
Result<std::shared_ptr<ImageAdapter>>ImageAdapter::create(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,Digest implementation,Digest reference,vxnn::EvidenceScope scope,bool diagnostic){
 return create_bound(std::move(catalog),caps,implementation,reference,scope,diagnostic,{});
}
Result<std::shared_ptr<ImageAdapter>>ImageAdapter::create_bound(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,Digest implementation,Digest reference,vxnn::EvidenceScope scope,bool diagnostic,std::shared_ptr<ResidentLease>charge){
 using R=Result<std::shared_ptr<ImageAdapter>>;try{
 if(!catalog||!nonzero_digest(implementation)||!nonzero_digest(reference))return R::failure(error(EINVAL,"missing trusted identities"));
 const auto&hw=catalog->identity();
 if(!s8_joint_platform(hw)&&(hw.provider_flags!=1||!hw.device_generation||hw.compatible!=vxnn::content_digest("vortex,hw4-s8-demo-v1")||!hex_is(hw.hardware_contract,"1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d")||!hex_is(hw.bit,"94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1")||!hex_is(hw.xsa,"02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94")))return R::failure(error(EXDEV,"exact S8 image hardware required"));
 if((scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&scope!=vxnn::EvidenceScope::REAL_BOARD)||(scope==vxnn::EvidenceScope::REAL_BOARD&&!hw.board_identity_verified&&!charge))return R::failure(error(EPERM,"execution evidence scope unverified"));
 if(caps.ps_base!=0x10000000||caps.ps_bytes<(1u<<20)||caps.pl_base!=0x500000000ULL||caps.pl_bytes<VX_IMAGE_RESERVED_BYTES+1024ULL||caps.ps_dma_bits!=32||caps.gpu_address_bits!=32||caps.pl_address_bits!=40||caps.cycle_counter_bits!=32||caps.contexts!=1||caps.preempt||caps.max_refs_per_job<5||caps.max_args_bytes<64||caps.max_program_bytes<2048||caps.max_objects<3||!caps.max_programs||caps.alignment!=64)return R::failure(error(ENOSPC,"33 slots plus guards/private args and native capabilities required"));
 auto p=std::make_unique<Impl>();p->catalog=catalog;p->caps=caps;p->implementation=implementation;p->reference=reference;p->scope=scope;p->diagnostics=diagnostic;p->native_charge=std::move(charge);
 for(unsigned i=0;i<10;i++){
 auto&key=p->programs[i];key.namespace_id=VX_IMAGE_NAMESPACE;key.logical_profile=VX_IMAGE_PROFILE;key.logical_program=vx_image_program_ids[i];key.hardware_contract=hw.hardware_contract;std::copy_n(vx_image_code_sha[i],32,key.code.begin());std::copy_n(vx_image_schema_sha[i],32,key.schema.begin());
 auto binding=catalog->resolve(key);
 if(!binding)return R::failure(*binding.error);
 if(binding.value->code_bytes!=2048||binding.value->args_bytes!=64)return R::failure(error(EBADMSG,"frozen code/argument extent differs"));
 }
 return R::success(std::shared_ptr<ImageAdapter>(new ImageAdapter(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"adapter allocation"));}
}
Result<vxnn::Plan>ImageAdapter::prepare(const ImageRequest&r){
 using R=Result<vxnn::Plan>;auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.fault)return R::failure(*s.fault);
 if(s.state.busy)return R::failure(error(EBUSY,"pending image retained"));
 try{
 const bool geometry=(r.width==1280&&r.height==720)||(s.diagnostics&&r.width==256&&r.height==144);
 if(!valid(r.request)||!geometry||!r.input_id||!r.cookie_namespace||r.cookie_namespace>0x7fffffffu||!r.display_cookie||!r.rgba||r.rgba->size()!=uint64_t(r.width)*r.height*4)return R::failure(error(EINVAL,"bounded request/input/geometry identity"));
 // Own bytes before hashing; subsequent caller mutation cannot change admission.
 auto owned=std::make_shared<const std::vector<uint8_t>>(*r.rgba);vxnn::Bytes bytes{owned->data(),owned->size()};
 if(!opaque(bytes)||image_crc32(bytes)!=r.crc)return R::failure(error(EBADMSG,"input RGBA alpha or CRC differs"));
 auto input_sha=hash(bytes);auto state=s.state;
 if(!s.initialized){int rc=vx_image_init(&state,r.width,r.height);
 if(rc)return R::failure(error(-rc,"geometry initialization"));}
 if(state.width!=r.width||state.height!=r.height)return R::failure(error(EINVAL,"fixed cache geometry changed"));
 bool changed=r.input_id!=state.input_id;
 if(!changed){if(input_sha!=s.input_sha||r.crc!=state.input_crc||!same_rect(r.rectangle,state.rect))return R::failure(error(ESTALE,"input ID immutable bytes/rect identity differs"));}
 else{int rc=vx_image_commit(&state,&r.rectangle,r.input_id,r.crc);
 if(rc)return R::failure(error(-rc,"input epoch commit rejected"));}
 vx_image_plan recipe{};int rc=vx_image_prepare(&state,&r.params,&recipe);
 if(rc)return R::failure(error(-rc,"recipe rejected"));
 vxnn::Plan plan;plan.caps=s.caps;plan.policy.hardware=s.catalog->identity();plan.resident_bytes=VX_IMAGE_RESERVED_BYTES;plan.total_jobs=recipe.count;plan.evidence_bytes=uint64_t(recipe.count)*512;
 std::ostringstream identity;identity<<"IMAGE_RGBA_V1:"<<vxnn::digest_hex(s.implementation)<<":"<<vxnn::digest_hex(s.reference)<<":"<<vxnn::digest_hex(input_sha)<<":"<<r.input_id<<":"<<r.width<<":"<<r.height<<":"<<r.rectangle.x<<":"<<r.rectangle.y<<":"<<r.rectangle.w<<":"<<r.rectangle.h<<":"<<r.params.style<<":"<<r.params.preset<<":"<<r.params.block<<":"<<recipe.cache_hit<<":"<<vxnn::digest_hex(s.catalog->sha())<<":"<<s.catalog->identity().device_generation;
 plan.model_id=s.implementation;plan.numeric_id=s.reference;
 for(unsigned i=0;i<33;i++){
 vxnn::PlannedTensor t;t.desc.id=i+1;t.desc.dtype=vxnn::DType::U8;t.desc.layout=i==10?"IMAGE_TABLE_U8":"RGBA8888_SLOT";t.desc.bytes=i==10?65536:uint64_t(r.width)*r.height*4;t.desc.shape={t.desc.bytes};t.desc.access=i==10?1:3;t.desc.constant=i==10;t.first=0;t.last=recipe.count;plan.tensors.push_back(std::move(t));
 }
 std::array<bool,10>used{};
 for(unsigned i=0;i<recipe.count;i++){
 const auto op=recipe.ops[i];vxnn::PlannedNode node;node.node.id=i+1;node.node.key={"IMAGE_OP_"+std::to_string(op),"1",s.implementation};node.job_count=1;node.evidence_bytes=512;node.node.program_requirements.push_back(s.programs[op].code);
 image_job wire{};vx_image_view views[5]{};rc=vx_image_lower(op,&recipe.jobs[i],&wire,views);
 if(rc)return R::failure(error(-rc,"native argument lowering failed"));
 for(unsigned j=0;j<5;j++){if(views[j].access&1)node.node.inputs.push_back(views[j].slot+1);
 if(views[j].access&2)node.node.outputs.push_back(views[j].slot+1);}
 node.node.parameters["op"]=uint64_t(op);plan.nodes.push_back(std::move(node));
 if(!used[op]){used[op]=true;plan.programs.push_back(s.programs[op]);}
 identity<<":"<<vxnn::digest_hex(hash({reinterpret_cast<const uint8_t*>(&recipe.jobs[i]),sizeof(image_job)}));
 }
 uint32_t slot=0,offset=0;rc=vx_image_slot(recipe.output_token,r.width*r.height*4,0,&slot,&offset);
 if(rc)return R::failure(error(-rc,"output logical slot"));
 vxnn::PlannedNode transfer;transfer.node.id=recipe.count+1;transfer.node.key={"IMAGE_TRANSFER_PRESENT","1",s.implementation};transfer.node.inputs={slot+1};plan.nodes.push_back(std::move(transfer));plan.inputs={1};plan.outputs={slot+1};plan.plan_id=vxnn::content_digest(identity.str());
 ImageResult result;result.request=r.request;result.plan=plan.plan_id;result.input_sha=input_sha;result.gpu_jobs=recipe.count;result.no_gpu_work=!recipe.count;result.cache_hit=recipe.cache_hit;result.gpu_complete=!recipe.count;result.scope=s.scope;
 ImageRequest request=r;request.rgba=owned;
 auto checkpoint=std::make_unique<Impl::Checkpoint>();checkpoint->state=s.state;checkpoint->recipe=s.recipe;checkpoint->request=s.request;checkpoint->result=s.result;checkpoint->input=s.input;checkpoint->input_sha=s.input_sha;checkpoint->output_hashes=s.output_hashes;checkpoint->output_valid=s.output_valid;checkpoint->initialized=s.initialized;
 // Allocate everything before publishing the metadata transaction.
 if(changed){s.output_hashes={};s.output_valid={};}
 s.state=state;s.recipe=recipe;s.input_sha=input_sha;s.input=std::move(owned);s.request=std::move(request);s.result=std::move(result);s.initialized=true;s.checkpoint=std::move(checkpoint);return R::success(std::move(plan));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded plan/input allocation"));}
}
Result<ImageResult>ImageAdapter::accept(const WorkOutcome&o){
 using R=Result<ImageResult>;auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.fault)return R::failure(*s.fault);
 if(!s.state.busy)return R::failure(error(ESTALE,"no active image"));
 const unsigned i=s.state.accepted;
 if(i>=s.recipe.count||!same(o.request,s.request.request)||o.node!=i+1||o.work_index!=i||o.status!=RequestStatus::COMPLETED||o.result||!o.acknowledged||!o.retired||o.legacy||!o.completion)return R::failure(s.fail(EPROTO,"successful matching once ACK/retirement required"));
 const auto&c=*o.completion;const uint64_t cookie=(uint64_t(s.request.cookie_namespace)<<32)+i+1;
 if(c.cookie!=cookie||!c.sequence||c.sequence<=s.last_sequence||c.status!=3||c.result)return R::failure(s.fail(ESTALE,"original completion identity/status differs"));
 if(s.scope==vxnn::EvidenceScope::REAL_BOARD){if(!c.cycles_valid||!c.raw_cycles||!*c.raw_cycles||*c.raw_cycles>UINT64_MAX-s.result.gpu_raw_cycles.value_or(0))return R::failure(s.fail(ENODATA,"physical raw cycles missing/overflow"));}
 else if(c.cycles_valid||c.raw_cycles)return R::failure(s.fail(EPROTO,"host adapter cannot claim physical GPU cycles"));
 int rc=vx_image_accept(&s.state,&s.recipe,i,0);
 if(rc)return R::failure(s.fail(-rc,"recipe ACK rejected"));
 s.last_sequence=c.sequence;s.result.completed_gpu=s.state.accepted;s.result.gpu_complete=s.state.accepted==s.recipe.count;
 if(s.scope==vxnn::EvidenceScope::REAL_BOARD)s.result.gpu_raw_cycles=s.result.gpu_raw_cycles.value_or(0)+*c.raw_cycles;
 return R::success(s.result);
}
Result<ImageResult>ImageAdapter::present(vxnn::Bytes frame,const DisplayReceipt&d){
 using R=Result<ImageResult>;auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.fault)return R::failure(*s.fault);
 if(!s.state.busy||!s.result.gpu_complete)return R::failure(error(EBUSY,"all GPU completions required before presentation"));
 if(!display_valid(d,s.request.display_cookie,s.scope)||!d.presented||d.result)return R::failure(error(EPROTO,"matching presented/retired display receipt required"));
 if(frame.size!=uint64_t(s.request.width)*s.request.height*4||!opaque(frame))return R::failure(s.fail(EBADMSG,"output RGBA extent/alpha differs"));
 const auto digest=hash(frame);const auto crc=image_crc32(frame);uint32_t slot=0,offset=0;int rc=vx_image_slot(s.recipe.output_token,uint32_t(frame.size),0,&slot,&offset);
 if(rc)return R::failure(s.fail(-rc,"output token"));
 if((s.recipe.cache_hit&&(!s.output_valid[slot]||s.output_hashes[slot]!=digest))||(!s.recipe.params.style&&s.input_sha!=digest))return R::failure(s.fail(EBADMSG,"cached/original output full SHA differs"));
 rc=vx_image_finish(&s.state,&s.recipe,crc);
 if(rc)return R::failure(s.fail(-rc,"output CRC/cache commit rejected"));
 s.output_hashes[slot]=digest;s.output_valid[slot]=true;s.result.output_sha=digest;s.result.output_crc=crc;s.result.displayed=true;s.checkpoint.reset();return R::success(s.result);
}
Result<ImageResult>ImageAdapter::presentation_failed(const DisplayReceipt&d,bool restored){
 using R=Result<ImageResult>;auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.fault)return R::failure(*s.fault);
 if(!s.state.busy||!s.result.gpu_complete||!restored||d.cookie!=s.request.display_cookie||d.presented||!d.result)return R::failure(error(EBUSY,"matching failed display and explicit restoration required"));
 const auto expected=s.scope==vxnn::EvidenceScope::REAL_BOARD?vxnn::ClockDomain::LINUX_MONOTONIC:vxnn::ClockDomain::HOST_MONOTONIC;
 if(d.clock_domain!=expected||!d.submitted_ns)return R::failure(error(EPROTO,"display failure clock identity differs"));
 int rc=vx_image_present_failed(&s.state,&s.recipe);
 if(rc)return R::failure(s.fail(-rc,"display failure retirement rejected"));
 if(!s.recipe.cache_hit&&s.recipe.params.style){uint32_t slot=0,offset=0;rc=vx_image_slot(s.recipe.output_token,s.request.width*s.request.height*4,0,&slot,&offset);
 if(rc)return R::failure(s.fail(-rc,"failed output slot"));
 s.output_valid[slot]=false;}
 s.result.first_error=error(d.result<0?-d.result:d.result,"display failed; state restored, GPU intermediates retained");s.checkpoint.reset();return R::success(s.result);
}
Result<void>ImageAdapter::cancel_pending(const RequestKey&key,bool never_started){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.fault)return Result<void>::failure(*s.fault);
 if(!s.state.busy||!same(key,s.request.request)||!s.checkpoint)return Result<void>::failure(error(ESTALE,"cancelled pending identity differs"));
 if(never_started){
  if(s.state.accepted)return Result<void>::failure(error(EBUSY,"cannot roll back acknowledged GPU work"));
  auto old=std::move(s.checkpoint);s.state=old->state;s.recipe=old->recipe;s.request=std::move(old->request);s.result=std::move(old->result);s.input=std::move(old->input);s.input_sha=old->input_sha;s.output_hashes=old->output_hashes;s.output_valid=old->output_valid;s.initialized=old->initialized;
 }else{
  int rc=vx_image_cancel_current(&s.state,&s.recipe);if(rc)return Result<void>::failure(error(-rc,"cancelled cache invalidation rejected"));
  s.output_hashes={};s.output_valid={};s.checkpoint.reset();
 }
 return Result<void>::success();
}
Result<vx_image_plan>ImageAdapter::recipe()const{auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.initialized)return Result<vx_image_plan>::failure(error(ENODATA,"no recipe"));
 return Result<vx_image_plan>::success(s.recipe);}
Result<std::shared_ptr<const std::vector<uint8_t>>>ImageAdapter::input_snapshot()const{auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.input)return Result<std::shared_ptr<const std::vector<uint8_t>>>::failure(error(ENODATA,"no input"));
 return Result<std::shared_ptr<const std::vector<uint8_t>>>::success(s.input);}
}
