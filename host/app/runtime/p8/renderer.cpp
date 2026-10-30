#include <vxunified/p8.hpp>
#include <vxunified/texture.hpp>
#include <vxl/hunet.h>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include "generated/p8/assets.h"
#include "vendor/p8/theme-palette-reentrant.h"
namespace vxunified {
namespace {
vxnn::Error error(int code,const char*text){return {code,"P8_ASSET",text};}
std::string hex(const Digest&d){std::string s;for(auto b:d){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
Digest hash(vxnn::Bytes b){Digest d{};vxl_hunet_hash(b.data,b.size,d.data());return d;}
using Bank=std::map<std::string,std::vector<uint32_t>>;
const std::vector<uint32_t>&get(const Bank&b,const std::string&n){return b.at(n+".bin");}
struct Material {const uint32_t*material_rgb=nullptr,*material_mix=nullptr;};
// A private per-frame translation of the frozen member functions. No MMIO or
// fixed bare-metal addresses are used. The exact extraction edits are audited.
class Math {
 P8Frame&frame;const uint32_t*cm_raw,*cm_indices,*cm_background,*cm_background_high,*cm_matrices;
 uint32_t*cm_projected,*cm_directory,*cm_refs,*cm_texture,*cm_colors,*cm_depths,*cm_pretri;
 std::vector<uint32_t>theme_small;std::vector<uint32_t>owners;uint32_t*cm_owners;uint32_t cm_active_refs=0,cm_theme_palette[10]{};
 Material material;const Material*cm_asset=&material;const int cm_validate=1;
 static int32_t v3_floor256(int64_t n){return int32_t(n>=0?n/256:-((-n+255)/256));}
 static int32_t p4_div_signed(int32_t n,int32_t d){return n/d;}
 static int fail(const char*,uint32_t,uint32_t,uint32_t){return -1;}
 int cm_upload_used(uint32_t b,uint32_t words){return b>=10||words>frame.buffers[b].size()?-1:0;}
 uint32_t cm_material_color(uint32_t t){if(!material.material_rgb)return 0;auto region=material.material_rgb[t]>>24;return cm_theme_palette[region<10?region:4];}
#define CM_SLOTS 16064u
#define CM_ACTIVE_SLOTS (this->frame.geometry.active_slots)
#define CM_WIDTH (this->frame.geometry.width)
#define CM_TEX_SIZE (this->frame.geometry.texture_width)
#define CM_PIXELS (CM_WIDTH*CM_WIDTH)
#define CM_TILES ((CM_WIDTH/4u)*(CM_WIDTH/4u))
#define CM_FACES (this->frame.geometry.faces)
#define CM_REF_CAP 262144u
#include "vendor/p8/math-members.inc"
#undef CM_SLOTS
#undef CM_ACTIVE_SLOTS
#undef CM_WIDTH
#undef CM_TEX_SIZE
#undef CM_PIXELS
#undef CM_TILES
#undef CM_FACES
#undef CM_REF_CAP
public:
 Math(P8Frame&f,const Bank&b):frame(f),owners(f.geometry.width*f.geometry.width){
  auto&v=f.buffers;cm_raw=v[3].data();cm_indices=v[5].data();cm_projected=v[4].data();cm_directory=v[6].data();cm_refs=v[7].data();cm_texture=v[2].data();cm_colors=v[0].data();cm_depths=v[1].data();cm_pretri=v[8].data();cm_owners=owners.data();
  cm_background=get(b,"cm_background").data();cm_background_high=get(b,"cm_background_high").data();cm_matrices=get(b,"cm_matrices").data();
  if(f.scene.object==P8Object::TERRACOTTA){material.material_rgb=get(b,"cm_warrior_material_rgb").data();material.material_mix=get(b,"cm_warrior_material_mix").data();}
  if(f.texture){auto low=f.texture->lod(0);theme_small.resize(low.size/4);std::memcpy(theme_small.data(),low.data,low.size);cm_theme_colors(theme_small.data(),cm_theme_palette);}
  else{cm_theme_colors(get(b,"cm_qinghua_texture").data(),cm_theme_palette);}
 }
 void project(){cm_project_gold(frame.scene.pose);}
 int bins(){const int rc=cm_build_pretri()||cm_build_tiles();frame.active_refs=cm_active_refs;return rc;}
 void reference(){cm_golden();}
};
P8Geometry description(unsigned i){return {i==0?398u:i==1?401u:16056u,i==0?768u:i==1?648u:30926u,i==2?16064u:448u,i==2?512u:256u,i==2?2048u:512u};}
bool same_geometry(const P8Geometry&a,const P8Geometry&b){return a.vertices==b.vertices&&a.faces==b.faces&&a.active_slots==b.active_slots&&a.width==b.width&&a.texture_width==b.texture_width;}
}
struct P8Assets::Impl {Digest identity{};Bank bank;};
P8Assets::P8Assets(std::shared_ptr<const Impl>p):impl_(std::move(p)){}
Result<std::shared_ptr<const P8Assets>>P8Assets::load(const std::string&dir){
 using R=Result<std::shared_ptr<const P8Assets>>;
 try{
 auto p=std::make_shared<Impl>();vxl_hunet_sha identity;vxl_hunet_sha_init(&identity);
 for(const auto&row:p8_files){
  const std::string path=dir+"/"+row.name;int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return R::failure(error(errno,"asset open"));
  struct Close {int fd;~Close(){close(fd);}}closer{fd};struct stat st{};
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=off_t(row.words*4))return R::failure(error(EBADMSG,"asset kind/extent"));
  std::vector<uint32_t>words(row.words);size_t pos=0;
  while(pos<words.size()*4){auto n=read(fd,reinterpret_cast<uint8_t*>(words.data())+pos,words.size()*4-pos);if(n<=0)return R::failure(error(EIO,"asset short read"));pos+=size_t(n);}
  uint8_t extra;auto tail=read(fd,&extra,1);if(tail!=0)return R::failure(error(EBADMSG,"asset grew during read"));
  auto sum=hash({reinterpret_cast<const uint8_t*>(words.data()),words.size()*4});if(hex(sum)!=row.sha)return R::failure(error(EBADMSG,"asset SHA256 differs"));
  vxl_hunet_sha_update(&identity,row.name,std::strlen(row.name));vxl_hunet_sha_update(&identity,sum.data(),sum.size());p->bank.emplace(row.name,std::move(words));
 }
 vxl_hunet_sha_final(&identity,p->identity.data());
 // Validate every actual index before the original math can dereference it.
 const char*names[]={"cm_porcelain_indices","cm_paperbag_indices","cm_terracotta_scan_indices"};
 for(unsigned i=0;i<3;i++){const auto g=description(i);const auto&indices=get(p->bank,names[i]);for(unsigned j=0;j<g.faces*3;j++)if(indices[j]>=g.vertices)return R::failure(error(EBADMSG,"mesh index out of range"));}
 return R::success(std::shared_ptr<const P8Assets>(new P8Assets(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded asset bank"));}
}
Result<P8Geometry>P8Assets::geometry(P8Object object)const{
 if(unsigned(object)>2)return Result<P8Geometry>::failure(error(EINVAL,"asset id"));
 return Result<P8Geometry>::success(description(unsigned(object)));
}
Result<P8Frame>P8Assets::prepare(const SceneState&s,bool reference)const{return prepare_bound(s,nullptr,reference,false);}
Result<P8Frame>P8Assets::prepare_bound(const SceneState&s,std::shared_ptr<const TexturePackage>texture,bool reference,bool redraw,bool defer_derived)const{
 auto owned=prepare_owned(s,std::move(texture),reference,redraw,defer_derived);if(!owned)return Result<P8Frame>::failure(*owned.error);
 return Result<P8Frame>::success(std::move(**owned.value));
}
Result<std::shared_ptr<P8Frame>>P8Assets::prepare_owned(const SceneState&source,std::shared_ptr<const TexturePackage>texture,bool reference,bool redraw,bool defer_derived)const{
 SceneState s=source;
 if(texture){auto rendered=scene_render_state(source,redraw);if(!rendered)return Result<std::shared_ptr<P8Frame>>::failure(*rendered.error);s=*rendered.value;}

 using R=Result<std::shared_ptr<P8Frame>>;auto valid=scene_frame_due(s);if(!valid)return R::failure(*valid.error);
 if(nonzero_digest(s.theme)!=bool(texture)||(texture&&s.theme!=texture->identity()))return R::failure(error(ENODATA,"custom theme requires matching admitted dual LOD texture"));
 try{
 auto owned=std::make_shared<P8Frame>();auto&f=*owned;f.texture=std::move(texture);f.scene=s;f.geometry=description(unsigned(s.object));const auto&g=f.geometry;const auto&b=impl_->bank;
 const size_t sizes[]={g.width*g.width,g.width*g.width,g.texture_width*g.texture_width,128512,128512,98304,(g.width/4)*(g.width/4)*2,262144,2097152,g.width*g.width};
 for(unsigned i=0;i<10;i++)f.buffers[i].resize(sizes[i]);
 const char*raws[]={"cm_porcelain_raw","cm_paperbag_raw","cm_terracotta_scan_raw"};const char*indices[]={"cm_porcelain_indices","cm_paperbag_indices","cm_terracotta_scan_indices"};
 f.buffers[2]=get(b,g.width==512?"cm_qinghua_high_texture":"cm_qinghua_texture");f.buffers[3]=get(b,raws[unsigned(s.object)]);f.buffers[5]=get(b,indices[unsigned(s.object)]);f.buffers[9]=get(b,g.width==512?"cm_background_high":"cm_background");
 if(f.texture){auto lod=f.texture->lod(g.width==512?1:0);if(lod.size!=f.buffers[2].size()*4)return R::failure(error(EBADMSG,"native texture extent"));std::memcpy(f.buffers[2].data(),lod.data,lod.size);}
 std::copy_n(get(b,"cm_matrices").data()+12*s.pose,12,f.matrix.begin());
 std::fill(f.buffers[0].begin(),f.buffers[0].end(),0xdeadbeef);std::fill(f.buffers[1].begin(),f.buffers[1].end(),0xdeadbeef);
 if(defer_derived&&reference)return R::failure(error(EINVAL,"reference requires derived geometry"));
  if(!defer_derived){Math math(f,b);math.project();if(math.bins())return R::failure(error(ERANGE,"frozen pretri/bin range"));if(reference)math.reference();}f.cpu_reference=reference;
 vxl_hunet_sha id;vxl_hunet_sha_init(&id);vxl_hunet_sha_update(&id,impl_->identity.data(),32);const uint32_t meta[]={unsigned(s.object),s.pose,g.width,g.texture_width};vxl_hunet_sha_update(&id,meta,sizeof(meta));if(f.texture){vxl_hunet_sha_update(&id,f.texture->identity().data(),32);vxl_hunet_sha_update(&id,f.texture->source_result_sha().data(),32);vxl_hunet_sha_update(&id,f.texture->source_tensor_sha().data(),32);auto scope=uint32_t(f.texture->source_scope());vxl_hunet_sha_update(&id,&scope,4);}vxl_hunet_sha_final(&id,f.input_sha.data());return R::success(std::move(owned));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded frame buffers"));}
}
Result<void>P8Assets::rebin(P8Frame&f,vxnn::Bytes actual,bool active_only)const{
 // Rebin validates an already chosen render pose; no animation advancement.
 auto state=f.scene;if(f.texture){state.paused=false;state.remaining=1;}
 auto expected=prepare_owned(state,f.texture,false,false);if(!expected)return Result<void>::failure(*expected.error);
 auto&e=**expected.value;
 const uint64_t required=active_only?uint64_t(e.geometry.active_slots)*8*4:e.buffers[4].size()*4;
  if(required>e.buffers[4].size()*4||!same_geometry(f.geometry,e.geometry)||f.input_sha!=e.input_sha||!actual.data||actual.size!=required||std::memcmp(actual.data,e.buffers[4].data(),actual.size))return Result<void>::failure(error(EBADMSG,"transform readback identity/numeric mismatch"));
 // Do not trust public mutable CPU buffers as source addresses or triangle ids.
 for(unsigned i:{2u,3u,5u,9u})if(f.buffers[i]!=e.buffers[i])return Result<void>::failure(error(ESTALE,"immutable frame input changed"));
 // Commit only after all checks. Derived bins use the already validated actual
 // projection, byte-identical to the independent integer transform reference.
 for(unsigned i:{4u,6u,7u,8u}){f.buffers[i]=std::move(e.buffers[i]);}
 f.active_refs=e.active_refs;return Result<void>::success();
}
Result<P8Frame>P8Assets::prepare_for_device(const SceneState&s,std::shared_ptr<const TexturePackage>texture,bool redraw)const{if(redraw&&!texture)return Result<P8Frame>::failure(error(ENODATA,"redraw requires admitted texture"));return prepare_bound(s,std::move(texture),false,redraw,true);}
const Digest&P8Assets::identity()const{return impl_->identity;}
Result<P8Frame>P8Assets::prepare_texture(const SceneState&s,std::shared_ptr<const TexturePackage>t,bool reference,bool redraw)const{if(!t)return Result<P8Frame>::failure(error(ENODATA,"missing admitted texture"));return prepare_bound(s,std::move(t),reference,redraw);}
}
