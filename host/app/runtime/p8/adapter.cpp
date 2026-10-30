#include <vxunified/p8.hpp>
#include <vxunified/texture.hpp>
#include <vxl/hunet.h>
#include <cstring>
#include "generated/p8/programs.h"
namespace vxunified {
namespace {
Digest hash(vxnn::Bytes b){Digest d{};vxl_hunet_hash(b.data,b.size,d.data());return d;}
Digest literal(const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[2*i])<<4)|n(s[2*i+1]);}return d;}
vxnn::Error err(int c,const char*s){return {c,"P8_ADAPTER",s};}
}
struct P8Adapter::Impl {std::shared_ptr<const InstalledCatalog>catalog;DeviceCaps caps;std::shared_ptr<const P8Assets>assets;vxnn::EvidenceScope scope;std::array<ProgramKey,4>programs;std::shared_ptr<ResidentLease>native_charge;};
P8Adapter::P8Adapter(std::unique_ptr<Impl>p):impl_(std::move(p)){}P8Adapter::~P8Adapter()=default;
Result<std::shared_ptr<P8Adapter>>P8Adapter::create(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,std::shared_ptr<const P8Assets>assets,vxnn::EvidenceScope scope){
 return create_bound(std::move(catalog),caps,std::move(assets),scope,{});
}
Result<std::shared_ptr<P8Adapter>>P8Adapter::create_bound(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,std::shared_ptr<const P8Assets>assets,vxnn::EvidenceScope scope,std::shared_ptr<ResidentLease>charge){
 using R=Result<std::shared_ptr<P8Adapter>>;
 if(!catalog||!assets)return R::failure(err(EINVAL,"missing trusted inputs"));
 const auto&hw=catalog->identity();const char*compatible="vortex,w4-graphics-p8-v1";
 if(!s8_joint_platform(hw)&&(hw.provider_flags!=2||!hw.device_generation||hw.hardware_contract!=literal("22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012")||hw.bit!=literal("faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc")||hw.xsa!=literal("08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536")||hw.compatible!=hash({reinterpret_cast<const uint8_t*>(compatible),std::strlen(compatible)})))return R::failure(err(EXDEV,"exact frozen W4/P8 identity required"));
 if((scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&scope!=vxnn::EvidenceScope::REAL_BOARD)||(scope==vxnn::EvidenceScope::REAL_BOARD&&!hw.board_identity_verified&&!charge))return R::failure(err(EPERM,"scope or physical identity unverified"));
 if(caps.ps_base!=0x10000000||caps.ps_bytes<1048576||caps.pl_base!=0x500000000ULL||caps.pl_bytes<33554432||caps.ps_dma_bits!=32||caps.gpu_address_bits!=32||caps.pl_address_bits!=40||caps.cycle_counter_bits!=32||caps.contexts!=1||caps.preempt||caps.max_refs_per_job<7||caps.max_args_bytes<64||caps.max_program_bytes<2048||caps.max_objects<4||!caps.max_programs||caps.alignment!=64)return R::failure(err(ENOSPC,"P8 native resources and seven data refs plus separate args required"));
 try{
 auto p=std::make_unique<Impl>();p->catalog=std::move(catalog);p->caps=caps;p->assets=std::move(assets);p->scope=scope;p->native_charge=std::move(charge);
 for(unsigned i=0;i<4;i++){auto&key=p->programs[i];key.namespace_id=2;key.logical_profile=5;key.logical_program=p8_program_ids[i];key.hardware_contract=hw.hardware_contract;std::copy_n(p8_code_sha[i],32,key.code.begin());std::copy_n(p8_schema_sha[i],32,key.schema.begin());auto binding=p->catalog->resolve(key);if(!binding)return R::failure(*binding.error);if(binding.value->code_bytes!=2048||binding.value->args_bytes!=64)return R::failure(err(EBADMSG,"frozen code or args extent differs"));}
 return R::success(std::shared_ptr<P8Adapter>(new P8Adapter(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure(err(ENOMEM,"bounded adapter metadata"));}
}
Result<vxnn::Plan>P8Adapter::prepare_frame(const SceneState&s)const{
 using R=Result<vxnn::Plan>;const auto&p=*impl_;auto valid=scene_frame_due(s);if(!valid)return R::failure(*valid.error);
 if(!*valid.value)return R::failure(err(EAGAIN,"paused or idle scene has no frame due"));
 if(nonzero_digest(s.theme))return R::failure(err(ENODATA,"unbound dual LOD theme"));
 const uint32_t poses[]={0,100,25,101,50,102,75,103};if(s.object==P8Object::TERRACOTTA&&std::find(std::begin(poses),std::end(poses),s.pose)==std::end(poses))return R::failure(err(EOPNOTSUPP,"native512 fixed eight poses only"));
 try{
 vxnn::Plan plan;plan.caps=p.caps;plan.policy.hardware=p.catalog->identity();plan.model_id=p.assets->identity();plan.numeric_id=hash({reinterpret_cast<const uint8_t*>("P8_Q8_Q2_EXACT_V1"),17});plan.total_jobs=2;plan.evidence_bytes=1024;plan.resident_bytes=33554432;const unsigned base=s.object==P8Object::TERRACOTTA?2:0;plan.programs={p.programs[base],p.programs[base+1]};
 const char*names[]={"P8_TRANSFORM","P8_CPU_BINS","P8_RASTER","P8_TRANSFER_PRESENT"};
 for(unsigned i=0;i<4;i++){vxnn::PlannedNode node;node.node.id=i+1;node.node.key={names[i],"1",plan.model_id};node.job_count=i==0||i==2;node.evidence_bytes=node.job_count*512;node.node.parameters["object"]=uint64_t(s.object);node.node.parameters["pose"]=uint64_t(s.pose);if(node.job_count)node.node.program_requirements={plan.programs[i==2?1:0].code};plan.nodes.push_back(std::move(node));}
 vxl_hunet_sha id;vxl_hunet_sha_init(&id);vxl_hunet_sha_update(&id,plan.model_id.data(),32);vxl_hunet_sha_update(&id,p.catalog->sha().data(),32);auto epoch=p.catalog->identity().device_generation;vxl_hunet_sha_update(&id,&epoch,sizeof(epoch));uint32_t meta[]={uint32_t(s.object),s.pose};vxl_hunet_sha_update(&id,meta,sizeof(meta));vxl_hunet_sha_final(&id,plan.plan_id.data());return R::success(std::move(plan));
 }catch(const std::bad_alloc&){return R::failure(err(ENOMEM,"bounded frame plan"));}
}
Result<ProgramKey>P8Adapter::program(uint32_t id)const{if(id<9||id>12)return Result<ProgramKey>::failure(err(ENOENT,"unregistered graphics program"));return Result<ProgramKey>::success(impl_->programs[id-9]);}
Result<vxnn::Plan>P8Adapter::prepare_texture_frame(const SceneState&s,std::shared_ptr<const TexturePackage>texture,bool redraw)const{
 using R=Result<vxnn::Plan>;if(!texture||!nonzero_digest(s.theme)||s.theme!=texture->identity())return R::failure(err(ENODATA,"matching admitted texture required"));
 auto rendered=scene_render_state(s,redraw);if(!rendered)return R::failure(*rendered.error);
 auto view=*rendered.value;view.theme={};view.paused=false;view.remaining=1;auto original=prepare_frame(view);if(!original)return original;
 try{auto plan=*original.value;vxl_hunet_sha id;vxl_hunet_sha_init(&id);vxl_hunet_sha_update(&id,plan.plan_id.data(),32);vxl_hunet_sha_update(&id,texture->identity().data(),32);vxl_hunet_sha_update(&id,texture->source_result_sha().data(),32);vxl_hunet_sha_update(&id,texture->source_tensor_sha().data(),32);uint32_t meta[]={uint32_t(texture->source_scope()),uint32_t(redraw)};vxl_hunet_sha_update(&id,meta,sizeof(meta));vxl_hunet_sha_final(&id,plan.plan_id.data());return R::success(std::move(plan));}catch(const std::bad_alloc&){return R::failure(err(ENOMEM,"bounded texture frame plan"));}
}
}
