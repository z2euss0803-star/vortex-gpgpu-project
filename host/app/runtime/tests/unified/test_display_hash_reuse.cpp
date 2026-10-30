#include "display_fixture.hpp"
#include "../../unified/display/internal.hpp"
#include <utility>
#include <cerrno>

namespace {
constexpr uint64_t rgb_bytes=1280ULL*720*3;
uint64_t source_bytes=0,scanout_bytes=0;
struct HashCounts {uint64_t rgb=0,source=0,scanout=0;} hashes;
}
extern "C" void __real_vxl_hunet_hash(const void*,vxl_u64,vxl_u8[32]);
extern "C" void __wrap_vxl_hunet_hash(const void*p,vxl_u64 n,vxl_u8 out[32]){
 if(n==rgb_bytes)++hashes.rgb;
 if(source_bytes&&n==source_bytes)++hashes.source;
 if(scanout_bytes&&n==scanout_bytes)++hashes.scanout;
 __real_vxl_hunet_hash(p,n,out);
}
static std::string actual_hex(const std::vector<uint8_t>&bytes){
 uint8_t digest[32];__real_vxl_hunet_hash(bytes.data(),bytes.size(),digest);
 std::string text(64,'0');const char*hex="0123456789abcdef";
 for(unsigned i=0;i<32;i++){text[i*2]=hex[digest[i]>>4];text[i*2+1]=hex[digest[i]&15];}
 return text;
}
static std::vector<uint8_t> raw_rgba(uint32_t n,unsigned seed=0){
 std::vector<uint8_t>raw(size_t(n)*n*4);
 for(size_t i=0;i<raw.size();i+=4){raw[i]=uint8_t(i*7+seed);raw[i+1]=uint8_t(i/137+seed*3);raw[i+2]=uint8_t(i/19+seed*11);raw[i+3]=255;}
 return raw;
}
static PublishedResult raw_source(const std::vector<uint8_t>&raw,uint32_t n){
 auto p=producer();p.lease.app=AppKind::P8;p.lease.artifacts[0].layout="RGBA8888";p.lease.artifacts[0].shape={n,n,4};p.lease.artifacts[0].bytes=raw.size();p.lease.artifacts[0].sha=sum(raw);return p;
}
static std::vector<uint8_t> page_oracle(const std::vector<uint8_t>&raw,uint32_t n){
 // P8's existing native contract:512 square at(384,104), including256x2.
 std::vector<uint8_t>rgb(size_t(rgb_bytes),0);
 for(uint32_t y=0;y<512;y++)for(uint32_t x=0;x<512;x++)for(unsigned c=0;c<3;c++)
  rgb[(size_t(y+104)*1280+x+384)*3+c]=raw[(size_t(y/(512/n))*n+x/(512/n))*4+c];
 return rgb;
}
struct FullDisplay:FakeDisplay {
 unsigned pad=0;uint32_t format=DP_XRGB8888;
 static int open_full(void*p,const dp_config*,dp_caps*c){++static_cast<FullDisplay*>(p)->opened;*c={1280,720,1,1,1,0};return 0;}
 static int create_full(void*p,uint32_t w,uint32_t h,dp_buffer*b){
  auto&f=*static_cast<FullDisplay*>(p);int i=f.created++;uint32_t pitch=w*4+f.pad*13;
  size_t bytes=size_t(pitch)*h+f.pad*9;f.maps[i]=static_cast<uint8_t*>(std::malloc(bytes));need(f.maps[i],"fake scanout allocation");
  std::memset(f.maps[i],0xa5,bytes);*b={uint32_t(i+1),w,h,pitch,f.format,bytes,f.maps[i]};return 0;
 }
 dp_ops ops(){return {open_full,create_full,initial_op,flip_op,receive_op,disable_op,destroy_op,close_op,this};}
};
static std::vector<uint8_t> scanout_oracle(const std::vector<uint8_t>&rgb,unsigned pad){
 uint32_t pitch=1280*4+pad*13;std::vector<uint8_t>bytes(size_t(pitch)*720+pad*9,0);
 for(uint32_t y=0;y<720;y++)for(uint32_t x=0;x<1280;x++){
  const auto*s=rgb.data()+(size_t(y)*1280+x)*3;auto*d=bytes.data()+size_t(y)*pitch+x*4;
  d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=255;
 }
 return bytes;
}
static std::shared_ptr<const DisplayCanvas> copy_as(const DisplayCanvas&sealed,const std::string&mode){
 auto out=std::make_shared<DisplayCanvas>(sealed);
 if(mode=="move"){DisplayCanvas stage(sealed);out=std::make_shared<DisplayCanvas>(std::move(stage));}
 if(mode=="copy_assign"){out=std::make_shared<DisplayCanvas>();*out=sealed;}
 if(mode=="move_assign"){DisplayCanvas stage(sealed);out=std::make_shared<DisplayCanvas>();*out=std::move(stage);}
 if(mode=="tamper")out->rgb[123]^=1;
 if(mode=="wrong_sha")out->pixel_sha[0]^=1;
 if(mode=="source")out->source_artifact[0]^=1;
 return out;
}
static void consumer_case(const std::string&mode,unsigned pad,uint32_t format,uint32_t n){
 auto raw=raw_rgba(n);auto p=raw_source(raw,n);auto l=get(raster_page_layout(p.lease,0,1280,720),"fixed native P8 layout");
 const auto expected_rgb=page_oracle(raw,n),expected_scanout=scanout_oracle(expected_rgb,pad);
 const auto rgb_sha=actual_hex(expected_rgb),scanout_sha=actual_hex(expected_scanout);
 int retired=0;auto s=store(retired);good(s->publish(p),"publish retained P8 source");FullDisplay fake;fake.pad=pad;fake.format=format;
 DisplayComposer compose=[&](const PublishedResult&source,const DisplayLayout&layout){
  auto sealed=compose_raster_page(source,0,{raw.data(),raw.size()},layout);if(!sealed||mode=="trusted"||mode=="observe")return sealed;
  return Result<std::shared_ptr<const DisplayCanvas>>::success(copy_as(**sealed.value,mode));
 };
 auto d=get(DisplayConsumer::create(s,fake.ops(),vxnn::ClockDomain::HOST_MONOTONIC,compose,journal()),"HOST fake owner");
 source_bytes=raw.size();scanout_bytes=expected_scanout.size();hashes={};
 const uint64_t begin=dp_now();auto r=d->present(p.lease,l);const uint64_t end=dp_now();const auto counts=hashes;
 const bool reject=mode=="tamper"||mode=="wrong_sha"||mode=="source";
 if(reject){
  need(!r&&r.error->code==EBADMSG,"copied canvas byte/digest/source tamper rejected");
  need(fake.cookie==0&&!d->snapshot().pending&&s->stats().consumer_leases==0,"rejection precedes submit and releases source");
  need(counts.source==1&&counts.scanout==0,"tamper does not skip retained-source gate or fabricate scanout");
 }else{
  need(bool(r),"real software present succeeds");
  need(!std::memcmp(fake.maps[1],expected_scanout.data(),expected_scanout.size()),"all RGB/BGRA/pitch/tail bytes match independent oracle");
  auto submitted=get(d->poll(),"submitted event");(void)submitted;
  auto observation=d->snapshot();need(observation.raw_events.size()==1,"raw submitted event retained");
  const auto&e=observation.raw_events.front();
  need(e.kind==DP_SUBMITTED&&e.frame_sha==rgb_sha&&e.scanout_sha==scanout_sha,"event binds source RGB and independently hashed actual scanout");
  need(e.width==1280&&e.height==720&&e.pitch==1280*4+pad*13&&e.conversion.source_bytes==rgb_bytes,"event metadata and actual conversion work retained");
  need(counts.source==1&&counts.scanout==1,"retained source and actual scanout each still hashed once");
  std::printf("DISPLAY_HASH_REUSE_OBSERVE mode=%s n=%u pad=%u format=%x rgb_hashes=%llu source_hashes=%llu scanout_hashes=%llu present_ns=%llu HOST_NOT_DP\n",
   mode.c_str(),n,pad,format,(unsigned long long)counts.rgb,(unsigned long long)counts.source,(unsigned long long)counts.scanout,(unsigned long long)(end-begin));std::fflush(stdout);
  if(mode=="trusted")need(counts.rgb==1,"sealed internal frame must hash RGB once, reusing composition SHA at frame and submit");
  else if(mode!="observe")need(counts.rgb==3,"unsealed copied/moved canvas must retain actual RGB verification at frame and submit");
  get(d->poll(),"HOST fake latch");get(d->poll(),"HOST fake retirement");
  auto original=get(s->take_result(p.lease.producer),"original producer receipt");
  need(original.displayed&&original.numeric==p.numeric&&original.raw_legacy_json==p.raw_legacy_json&&!original.gpu_raw_cycles,"HOST display does not upgrade producer numeric/GPU evidence");
 }
 good(s->reap_result(p.lease.producer),"client result release");good(d->restore(),"explicit fake restore");
 need(retired==1&&s->stats().consumer_leases==0&&fake.destroyed==2,"source retires only after known fake restoration");
}
static void cache_case(){
 RasterComposeCache cache;auto a=raw_rgba(512,1),b=raw_rgba(512,2);auto p=raw_source(a,512),q=raw_source(b,512);
 auto l=get(raster_page_layout(p.lease,0,1280,720),"cache layout");auto first=get(cache.compose(p,0,{a.data(),a.size()},l),"cache first sealed canvas");
 const auto first_bytes=first->rgb;const auto*address=first->rgb.data();
 auto other=get(cache.compose(q,0,{b.data(),b.size()},l),"held const canvas forces separate buffer");
 need(first->rgb==first_bytes&&other->rgb.data()!=address,"held const canvas never overwritten");
 first.reset();other.reset();auto reused=get(cache.compose(q,0,{b.data(),b.size()},l),"retired cache reseals new content");
 need(reused->rgb.data()==address&&cache.stats().buffer_reuses==1,"same private allocation reused only after readers retire");
 hashes={};auto f=get(display_detail::frame(q,*reused,l,1280,720),"reused canvas frame");
 need(hashes.rgb==0&&std::string(f.sha256)==actual_hex(page_oracle(b,512)),"cache refresh seal binds new source/digest without repeated RGB hash");
 auto bad=b;bad[0]^=1;need(!cache.compose(q,0,{bad.data(),bad.size()},l),"cache cannot reuse seal to skip RGBA source validation");
 puts("DISPLAY_HASH_CACHE_RESEAL_HELD_CONST_SOURCE_GATE_PASS_HOST_NOT_DP");
}
static void external_case(){
 FakeDisplay fake;dp_context c{};auto ops=fake.ops();dp_config cfg{4,2};need(!dp_open(&c,&cfg,&ops),"external fake open");
 auto bytes=canvas()->rgb;demo_frame f{};f.width=4;f.height=2;f.stride=12;f.bytes=bytes.size();f.pixels=bytes.data();
 std::memset(f.run_id,'1',32);std::memset(f.source_result_sha,'a',64);need(!demo_frame_hash(&f),"mutable external initial digest");
 bytes[0]^=1;need(dp_submit(&c,&f)==-EBADMSG&&fake.cookie==0,"external dp_submit rejects mutable bytes changed after hash");
 bytes[0]^=1;need(!dp_submit(&c,&f),"original external valid frame accepted");
 dp_event e{};need(!dp_poll(&c,0,&e)&&e.kind==DP_SUBMITTED,"external submitted event");const auto actual_scanout=std::vector<uint8_t>(fake.maps[1],fake.maps[1]+48);
 need(e.scanout_sha==actual_hex(actual_scanout),"external actual scanout digest retained");need(!dp_close(&c),"external fake close");
 puts("DISPLAY_EXTERNAL_MUTABLE_FRAME_SHA_GATE_AND_ACTUAL_SCANOUT_PASS_HOST_NOT_DP");
}
int main(int argc,char**argv){
 const std::string mode=argc>1?argv[1]:"trusted";
 if(mode=="cache"){cache_case();return 0;}if(mode=="external"){external_case();return 0;}
 for(uint32_t n:{256u,512u})for(unsigned pad:{0u,1u})for(uint32_t format:{DP_XRGB8888,DP_ARGB8888})consumer_case(mode,pad,format,n);
 std::printf("DISPLAY_HASH_REUSE_%s_PASS_HOST_NOT_GPU_DP_BOARD gpu_raw_cycles=null\n",mode.c_str());return 0;
}
