#include <vxunified/installation.hpp>
#include <vxunified/ingress_mux.hpp>
#include <vxunified/joint_bridge.hpp>
#include <vxunified/sealed_assets.hpp>
#include <vxunified/native_result_view.hpp>
#include <vxunified/native_result_export.hpp>
#include <vxunified/native_image_service.hpp>
#include <vxunified/native_p8_service.hpp>
#include <vxunified/journal.hpp>
#include <cstdio>
#include "nnrt/package/json.hpp"
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <set>
#include <map>
namespace vxunified {
namespace {
struct Fd {int n=-1;explicit Fd(int f=-1):n(f){}~Fd(){if(n>=0)close(n);}Fd(const Fd&)=delete;Fd&operator=(const Fd&)=delete;void reset(int f){if(n>=0)close(n);n=f;}};
bool path_ok(const std::string&p){if(p.size()<2||p.size()>4096||p[0]!='/'||p.back()=='/'||p.find('\0')!=std::string::npos) { return false; }size_t i=1;while(i<p.size()){auto j=p.find('/',i);if(j==std::string::npos)j=p.size();auto part=p.substr(i,j-i);if(part.empty()||part=="."||part=="..") { return false; }i=j+1;}return true;}
bool id_ok(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';});}

Result<std::string>output_directory(const StartupContext&c){
 if(!path_ok(c.private_dir)) { return Result<std::string>::failure({EINVAL,"INSTALLATION_FILE","private common owner path required"}); }Fd d(open(c.private_dir.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat s{};if(d.n<0||fstat(d.n,&s)<0||s.st_uid!=geteuid()||(s.st_mode&07777)!=0700) { return Result<std::string>::failure({EACCES,"INSTALLATION_FILE","same-uid common owner0700 directory required"}); }
 if(mkdirat(d.n,"outputs",0700)&&errno!=EEXIST) { return Result<std::string>::failure({errno,"INSTALLATION_FILE","create common private output storage"}); }Fd out(openat(d.n,"outputs",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(out.n<0||fstat(out.n,&s)<0||s.st_uid!=geteuid()||(s.st_mode&07777)!=0700) { return Result<std::string>::failure({EACCES,"INSTALLATION_FILE","private output directory validation"}); }return Result<std::string>::success(c.private_dir+"/outputs");
}
}
Result<NativeInstallation>decode_native_installation(const std::string&raw,const BackendIdentity&hw,const Digest&expected){
 using R=Result<NativeInstallation>;
 try{
  namespace j=vxnn::json;
  j::check(!raw.empty()&&raw.size()<=65536&&nonzero_digest(expected)&&vxnn::content_digest(raw)==expected,"sealed installation SHA/64KiB bound");
  j::check((s8_provider(hw)||p8_provider(hw)||s8_joint_platform(hw))&&hw.device_generation,"exact installed S8 or P8 provider and actual device epoch required");
  auto tree=j::parse(raw);auto*o=tree.get();const auto version=j::u32(j::member(o,"version"));
  j::check(version==1||version==2,"installation version1 or version2 required");
  if(version==1){j::fields(o,{"version","hardware","catalog","nn"});j::check(s8_provider(hw),"version1 native NN requires S8 provider");}
  else j::fields(o,{"version","hardware","catalog","nn","display","image","p8","mosaic_export"});
  auto*h=j::member(o,"hardware");j::fields(h,{"hardware_contract","bit","xsa","compatible","provider_flags"});
  j::check(j::digest(j::member(h,"hardware_contract"))==hw.hardware_contract&&j::digest(j::member(h,"bit"))==hw.bit&&j::digest(j::member(h,"xsa"))==hw.xsa&&j::digest(j::member(h,"compatible"))==hw.compatible&&j::u32(j::member(h,"provider_flags"))==hw.provider_flags,"complete installed hardware identity differs");
  NativeInstallation install;install.version=version;install.sha=expected;
  auto*c=j::member(o,"catalog");j::fields(c,{"path","sha256"});install.catalog_path=j::str(j::member(c,"path"));install.catalog_sha=j::digest(j::member(c,"sha256"));
  j::check(path_ok(install.catalog_path)&&nonzero_digest(install.catalog_sha),"installed catalog path/identity");
  auto*a=j::member(o,"nn");const auto count=j::count(a,version==1?2:9);std::set<std::string>ids;
  j::check((version==2||count>0)&&(!count||s8_provider(hw)),"nonempty version1 NN list and S8-only native models required");
  for(size_t i=0;i<count;++i){
   auto*n=j::at(a,i);j::fields(n,{"id","input_id","kind","package","model","plan","input","resident_bytes","input_bytes","job_budget_ns","family","presets"});InstalledNnModel m;
   m.id=j::str(j::member(n,"id"));m.input_id=j::str(j::member(n,"input_id"));m.package_directory=j::str(j::member(n,"package"));m.model=j::digest(j::member(n,"model"));m.plan=j::digest(j::member(n,"plan"));m.input=j::digest(j::member(n,"input"));m.resident_bytes=j::u64(j::member(n,"resident_bytes"));m.input_bytes=j::u64(j::member(n,"input_bytes"));m.job_budget_ns=j::u64(j::member(n,"job_budget_ns"));
   j::check(id_ok(m.id)&&id_ok(m.input_id)&&ids.insert(m.id).second&&path_ok(m.package_directory)&&nonzero_digest(m.model)&&nonzero_digest(m.plan)&&nonzero_digest(m.input)&&m.resident_bytes&&m.resident_bytes<=(1ULL<<30)&&m.input_bytes&&m.input_bytes<=(64ULL<<20)&&m.job_budget_ns&&m.job_budget_ns<=60000000000ULL,"installed NN identity/extent/deadline");
   auto kind=j::str(j::member(n,"kind"));auto*f=j::member(n,"family");j::check(kind=="hunet"||kind=="mosaic"||kind=="int16","compiled native family required");
   if(j::has(n,"presets")){auto*p=j::member(n,"presets");j::fields(p,{"directory","sha256"});InstalledNnPresets fixed;fixed.directory=j::str(j::member(p,"directory"));fixed.manifest=j::digest(j::member(p,"sha256"));j::check(kind=="hunet"&&m.input_bytes==196608&&path_ok(fixed.directory)&&nonzero_digest(fixed.manifest),"fixed sealed HUNet preset directory/manifest/shape required");m.presets=std::move(fixed);}
   if(kind=="mosaic")j::fields(f,{"code","schema","implementation","readonly"});else j::fields(f,{"code","schema","implementation"});
   auto code=j::digest(j::member(f,"code")),schema=j::digest(j::member(f,"schema")),implementation=j::digest(j::member(f,"implementation"));j::check(nonzero_digest(code)&&nonzero_digest(schema)&&nonzero_digest(implementation),"separately sealed operator family required");
   if(kind=="hunet")m.family=vxnn::HunetFamily{code,implementation,schema};
   else if(kind=="int16"){j::check(vxnn::digest_hex(schema)=="b4bcde7864f8b6cab955f2f98de2d558edefe27257a6465fe89aedba72a95f0d","existing integer family schema required");m.family=vxnn::Int16Family{code,implementation};}
   else{auto ro=j::digest(j::member(f,"readonly"));j::check(nonzero_digest(ro),"native INT16 read-only identity required");m.family=vxnn::MosaicFamily{code,ro,schema,implementation};}
   install.nn.push_back(std::move(m));
  }
  if(version==2){
   auto endpoint=[&](json_object*v,InstalledEndpoint&e){
    e.session=j::u32(j::member(v,"session"));e.display_base=j::u64(j::member(v,"display_base"));auto port=j::u32(j::member(v,"port"));e.address=j::str(j::member(v,"address"));e.job_budget_ns=j::u64(j::member(v,"job_budget_ns"));
    in_addr address{};char normalized[INET_ADDRSTRLEN]{};
    j::check(!e.address.empty()&&e.address.size()<INET_ADDRSTRLEN&&inet_pton(AF_INET,e.address.c_str(),&address)==1&&inet_ntop(AF_INET,&address,normalized,sizeof(normalized))&&e.address==normalized,"canonical IPv4 bind address required");
    j::check(e.session&&e.display_base&&e.display_base<=UINT32_MAX&&port&&port<=UINT16_MAX&&e.job_budget_ns&&e.job_budget_ns<=60000000000ULL,"bounded native session/display/UDP/deadline required");e.port=uint16_t(port);
   };
   auto*d=j::member(o,"display");
   if(d){j::fields(d,{"device","hunet"});InstalledDisplay display;display.device=j::str(j::member(d,"device"));const std::string prefix="/dev/dri/card";
    j::check(path_ok(display.device)&&display.device.compare(0,prefix.size(),prefix)==0,"DRM primary device required");auto number=display.device.substr(prefix.size());
    j::check(!number.empty()&&number.size()<=2&&(number.size()==1||number[0]!='0')&&std::all_of(number.begin(),number.end(),[](char v){return v>='0'&&v<='9';})&&std::stoul(number)<=63,"bounded DRM primary node number required");
    auto*view=j::has(d,"hunet")?j::member(d,"hunet"):nullptr;
    if(view){j::fields(view,{"directory","sha256"});InstalledHunetView h;h.directory=j::str(j::member(view,"directory"));h.manifest=j::digest(j::member(view,"sha256"));j::check(s8_provider(hw)&&path_ok(h.directory)&&nonzero_digest(h.manifest),"sealed S8 HUNet source view required");display.hunet=std::move(h);}
    install.display=std::move(display);
   }
   auto*image=j::member(o,"image");if(image){
    j::check(s8_provider(hw),"IMAGE requires native S8 provider");j::fields(image,{"implementation","reference","readonly","session","bundle","display_base","port","address","job_budget_ns"});InstalledImage m;endpoint(image,m);
    m.implementation=j::digest(j::member(image,"implementation"));m.reference=j::digest(j::member(image,"reference"));m.bundle=j::str(j::member(image,"bundle"));const auto bundle=j::digest(j::member(image,"bundle"));auto*ro=j::member(image,"readonly");j::fields(ro,{"path","sha256"});m.readonly_path=j::str(j::member(ro,"path"));m.readonly_sha=j::digest(j::member(ro,"sha256"));
    j::check(nonzero_digest(m.implementation)&&nonzero_digest(m.reference)&&nonzero_digest(bundle)&&nonzero_digest(m.readonly_sha)&&path_ok(m.readonly_path),"sealed IMAGE implementation/reference/source/table identities required");install.image=std::move(m);
   }
   auto*p8=j::member(o,"p8");if(p8){
    j::check(p8_provider(hw),"P8 requires its distinct W4 provider");j::fields(p8,{"assets_directory","assets","session","display_base","port","address","job_budget_ns","textures","startup_selftest_frames"});InstalledP8 m;endpoint(p8,m);m.assets_directory=j::str(j::member(p8,"assets_directory"));m.assets=j::digest(j::member(p8,"assets"));m.startup_selftest_frames=hw.provider_flags==4?1:100;if(j::has(p8,"startup_selftest_frames"))m.startup_selftest_frames=j::u32(j::member(p8,"startup_selftest_frames"));j::check(m.startup_selftest_frames==1||m.startup_selftest_frames==100,"explicit daily1 or maintenance100 startup policy required");
    if(j::has(p8,"textures")){auto*rows=j::member(p8,"textures");std::set<std::string>names;for(size_t i=0;i<j::count(rows,16);++i){auto*r=j::at(rows,i);j::fields(r,{"id","result_path","result_sha256","package_directory","package_id"});RegisteredTextureFile t;t.id=j::str(j::member(r,"id"));t.result_path=j::str(j::member(r,"result_path"));t.result_sha=j::digest(j::member(r,"result_sha256"));t.policy.package_directory=j::str(j::member(r,"package_directory"));t.policy.expected_package_id=j::digest(j::member(r,"package_id"));j::check(id_ok(t.id)&&names.insert(t.id).second&&path_ok(t.result_path)&&path_ok(t.policy.package_directory)&&nonzero_digest(t.result_sha)&&nonzero_digest(t.policy.expected_package_id),"unique sealed texture ID/path/content identity required");m.textures.push_back(std::move(t));}}
    j::check(path_ok(m.assets_directory)&&nonzero_digest(m.assets),"sealed P8 assets required");install.p8=std::move(m);
   }
   j::check(!install.nn.empty()||install.image||install.p8,"empty installation cannot report ready applications");
   auto*exporter=j::has(o,"mosaic_export")?j::member(o,"mosaic_export"):nullptr;
   if(exporter){j::fields(exporter,{"build_id"});InstalledMosaicExport m;m.build_id=j::str(j::member(exporter,"build_id"));j::check(s8_provider(hw)&&id_ok(m.build_id)&&std::any_of(install.nn.begin(),install.nn.end(),[](const InstalledNnModel&n){return std::holds_alternative<vxnn::MosaicFamily>(n.family);}),"declared image build and admitted S8 Mosaic model required for file export");install.mosaic_export=std::move(m);}
   j::check((!install.image&&!install.p8)||bool(install.display),"native media requires explicit display binding");
   if(install.display&&install.display->hunet)j::check(std::any_of(install.nn.begin(),install.nn.end(),[](const InstalledNnModel&m){return std::holds_alternative<vxnn::HunetFamily>(m.family);}),"HUNet display requires an admitted HUNet model");
  }
  return R::success(std::move(install));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"INSTALLATION","installation allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"INSTALLATION",e.what()});}
}
namespace {
struct HostDisplayBinding {dp_ops ops;std::shared_ptr<void>lifetime;};
struct InstallationConstruction {
 ApplicationSet&set;std::shared_ptr<NativeDisplay>display;bool committed=false;
 ~InstallationConstruction(){
  if(committed)return;
  try{
   if(set.quiesce){auto r=set.quiesce();if(!r){std::fprintf(stderr,"INSTALLATION_ROLLBACK_QUIESCE_ERROR %d %s\n",r.error->code,r.error->detail.c_str());return;}}
   if(display){auto r=display->retire();if(!r){std::fprintf(stderr,"INSTALLATION_ROLLBACK_DISPLAY_ERROR %d %s\n",r.error->code,r.error->detail.c_str());return;}}
   if(set.release_resources){auto r=set.release_resources();if(!r)std::fprintf(stderr,"INSTALLATION_ROLLBACK_RESOURCES_ERROR %d %s\n",r.error->code,r.error->detail.c_str());}
  }catch(...){std::fprintf(stderr,"INSTALLATION_ROLLBACK_EXCEPTION original resources retained by owners\n");}
 }
};
ApplicationBuilder installed_builder(const std::string&manifest,const std::string&sha_file,std::optional<HostDisplayBinding>host){return [manifest,sha_file,host](const StartupContext&ctx)->Result<ApplicationSet>{
 using R=Result<ApplicationSet>;
 try{
  if(host&&ctx.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST)return R::failure({EPERM,"INSTALLATION","injected display operations cannot create real-I/O evidence"});
  if(ctx.mode!=BackendMode::COMPUTE_V2||!ctx.compute_backend||ctx.runtime.expired()||ctx.boot_id.empty())return R::failure({EOPNOTSUPP,"INSTALLATION","actual common compute-v2 roots and boot identity required"});
  auto seal=read_sealed_native_asset(sha_file,65);if(!seal)return R::failure(*seal.error);auto hex=*seal.value;if(hex.size()==65&&hex.back()=='\n')hex.pop_back();if(hex.size()!=64)return R::failure({EINVAL,"INSTALLATION","exact installation SHA seal required"});
  auto sha_json=vxnn::json::parse("\""+hex+"\"");auto expected=vxnn::json::digest(sha_json.get());auto bytes=read_sealed_native_asset(manifest,65536);if(!bytes)return R::failure(*bytes.error);auto decoded=decode_native_installation(*bytes.value,ctx.identity,expected);if(!decoded)return R::failure(*decoded.error);const auto&install=*decoded.value;
  auto catalog_bytes=read_sealed_native_asset(install.catalog_path,65536);if(!catalog_bytes)return R::failure(*catalog_bytes.error);const auto&raw=*catalog_bytes.value;
  auto catalog=InstalledCatalog::decode({reinterpret_cast<const uint8_t*>(raw.data()),raw.size()},ctx.identity,install.catalog_sha);if(!catalog)return R::failure(*catalog.error);
  auto residency=ProgramResidency::create(*catalog.value,ctx.compute_backend);if(!residency)return R::failure(*residency.error);auto router=NativeRouter::create(ctx);if(!router)return R::failure(*router.error);auto output=output_directory(ctx);if(!output)return R::failure(*output.error);auto files=NativeFiles::create(ctx,*output.value);if(!files)return R::failure(*files.error);
  ApplicationSet set;set.installation_sha=install.sha;set.compute=(*router.value)->handlers();set.release_resources=[residency=*residency.value](){return residency->shutdown();};InstallationConstruction construction{set,{},false};
  if(!install.nn.empty()){
   auto nn=NativeNnLibrary::create(ctx,*router.value,*residency.value,*files.value);if(!nn)return R::failure(*nn.error);
   auto input_directory=(*nn.value)->set_input_directory(ctx.private_dir+"/input-inbox");if(!input_directory)return R::failure(*input_directory.error);
   auto previous_release=set.release_resources;set.release_resources=[library=*nn.value,previous_release](){auto r=library->shutdown();if(!r)return r;return previous_release?previous_release():Result<void>::success();};
   for(const auto&spec:install.nn){auto entry=(*nn.value)->entry(spec);if(!entry)return R::failure(*entry.error);RpcRequest q;q.command="submit";q.request={ctx.boot_id,"installation-preflight",spec.id};q.entry_id=spec.id;q.input_id=spec.input_id;q.parameters="{}";auto preflight=entry.value->prepare(q);if(!preflight)return R::failure(*preflight.error);set.entries.push_back(*entry.value);}
  }
  std::shared_ptr<NativeMosaicExporter>joint_exporter;
  if(install.mosaic_export){
   std::vector<NativeMosaicExportSpec>models;std::set<Digest>seen;
   for(const auto&spec:install.nn)if(auto*family=std::get_if<vxnn::MosaicFamily>(&spec.family))if(seen.insert(spec.model).second)models.push_back({spec.model,family->program_sha,family->implementation_sha,install.mosaic_export->build_id});
   auto made=NativeMosaicExporter::create(ctx,*files.value,std::move(models));if(!made)return R::failure(*made.error);auto exporter=*made.value;joint_exporter=exporter;
   set.export_result=[exporter,files=*files.value,context=ctx](Runtime&rt,const RpcRequest&q,const PublishedResult&p)->Result<std::string>{
    if(!files->owns_runtime(&rt)||q.artifact!=0)return Result<std::string>::failure({EXDEV,"INSTALLATION_EXPORT","same common Runtime and original output artifact0 required"});
    auto saved=exporter->export_result(p,q.destination_id);if(!saved)return Result<std::string>::failure(*saved.error);
    namespace j=vxnn::json;auto response=j::parse("{}");
    auto add=[](json_object*o,const char*k,json_object*v){if(!v)throw std::bad_alloc();if(json_object_object_add(o,k,v)){json_object_put(v);throw std::bad_alloc();}};
    auto artifact=[&](const char*name,const ResultArtifact&a){auto row=j::parse("{}");add(row.get(),"path",json_object_new_string_len(a.path.data(),int(a.path.size())));auto hex=vxnn::digest_hex(a.sha);add(row.get(),"sha256",json_object_new_string(hex.c_str()));add(row.get(),"bytes",json_object_new_uint64(a.bytes));add(response.get(),name,json_object_get(row.get()));};
    artifact("manifest",saved.value->manifest);artifact("tensor",saved.value->tensor);
    if(s8_joint_platform(context.identity)){auto inbox=context.private_dir+"/texture-inbox/"+vxnn::digest_hex(saved.value->manifest.sha);auto output=context.private_dir+"/outputs";add(response.get(),"texture_inbox",json_object_new_string(inbox.c_str()));add(response.get(),"file_root",json_object_new_string(output.c_str()));add(response.get(),"provider_flags",json_object_new_int(4));}
    return Result<std::string>::success(j::dump(response.get()));
   };
  }
  std::shared_ptr<const std::vector<uint8_t>>table;
  if(install.image){auto ro=read_sealed_native_asset(install.image->readonly_path,65536);if(!ro)return R::failure(*ro.error);if(ro.value->size()!=65536||vxnn::content_digest(*ro.value)!=install.image->readonly_sha)return R::failure({ESTALE,"INSTALLATION","installed IMAGE read-only table identity/size differs"});table=std::make_shared<const std::vector<uint8_t>>(ro.value->begin(),ro.value->end());}
  std::shared_ptr<NativeDisplay>display;
  if(install.display){
   auto journal=ctx.journal.lock();if(!journal)return R::failure({ESHUTDOWN,"INSTALLATION","common durable journal expired"});
    std::optional<NativeHunetViewSpec>spec;
    if(install.display->hunet){NativeHunetViewSpec h;h.directory=install.display->hunet->directory;h.manifest=install.display->hunet->manifest;std::set<Digest>models;std::map<std::pair<Digest,Digest>,Digest>page_bindings;for(const auto&m:install.nn)if(std::holds_alternative<vxnn::HunetFamily>(m.family)){models.insert(m.model);if(m.presets){auto pinned=page_bindings.emplace(std::make_pair(m.model,m.presets->manifest),m.plan);vxnn::json::check(pinned.second||pinned.first->second==m.plan,"conflicting page model/base plan binding");if(pinned.second)h.page_presets.push_back({m.presets->directory,m.presets->manifest,m.model,m.plan});}}h.models.assign(models.begin(),models.end());spec=std::move(h);}
    auto prepared_view=NativeResultView::create(ctx,*files.value,std::move(spec));if(!prepared_view)return R::failure(*prepared_view.error);auto view=*prepared_view.value;
    DisplayComposer compose=[view](const PublishedResult&p,const DisplayLayout&l){return view->compose(p,l);};
   DisplayJournal events=[journal,hw=ctx.identity,scope=ctx.scope](const DisplayObservation&r){return journal->save_display(hw,scope,r);};PersistResult stage=[journal](const PublishedResult&r){return journal->save_result_stage(r);};
   auto made=host?NativeDisplay::create(ctx,host->ops,compose,events,stage,host->lifetime):NativeDisplay::create_native(ctx,compose,events,stage,install.display->device);if(!made)return R::failure(*made.error);display=*made.value;construction.display=display;
   set.retire_display=[display](){return display->retire();};
    set.display=[display,view](Runtime&rt,const RpcRequest&,const PublishedResult&p)->Result<std::string>{
    if(!display->owns_runtime(&rt)){return Result<std::string>::failure({EXDEV,"INSTALLATION_DISPLAY","same common Runtime required"});}
     auto caps=display->capabilities();auto layout=view->layout(p,caps.width,caps.height);
    if(!layout){return Result<std::string>::failure(*layout.error);}
    auto shown=display->present_result(p,*layout.value);
    if(!shown){return Result<std::string>::failure(*shown.error);}
    return Result<std::string>::success("{\"displayed\":true,\"cookie\":"+std::to_string(shown.value->cookie)+"}");
   };
  }
  if(install.image){
   const auto&i=*install.image;NativeImageSpec spec;spec.implementation=i.implementation;spec.reference=i.reference;spec.readonly_table=table;spec.job_budget_ns=i.job_budget_ns;
   auto library=NativeImageLibrary::create(ctx,*router.value,*residency.value,*files.value,display,spec);if(!library)return R::failure(*library.error);auto service=NativeImageService::create(ctx,*library.value,i.session,i.bundle,i.display_base,i.port,i.address);if(!service)return R::failure(*service.error);auto bound=(*service.value)->bind(set);if(!bound)return R::failure(*bound.error);
  }
  if(install.p8){
   const auto&i=*install.p8;NativeP8Spec spec;spec.assets_directory=i.assets_directory;spec.assets=i.assets;spec.job_budget_ns=i.job_budget_ns;
   auto library=NativeP8Library::create(ctx,*router.value,*residency.value,*files.value,display,spec);if(!library)return R::failure(*library.error);auto service=NativeP8Service::create(ctx,*library.value,*router.value,i.session,i.display_base,i.port,i.address,i.startup_selftest_frames);if(!service)return R::failure(*service.error);
   if(install.image){
    ApplicationSet ingress;ingress.export_result=set.export_result;auto bound=(*service.value)->bind(ingress,*files.value,i.textures);if(!bound)return R::failure(*bound.error);
    auto combined=merge_native_ingress(set,std::move(ingress));if(!combined)return R::failure(*combined.error);
   }else{auto bound=(*service.value)->bind(set,*files.value,i.textures);if(!bound)return R::failure(*bound.error);}
   if(s8_joint_platform(ctx.identity)&&joint_exporter){auto previous=set.export_result;auto scene=*service.value;
    set.export_result=[previous,scene,exporter=joint_exporter,context=ctx,files=*files.value](Runtime&rt,const RpcRequest&q,const PublishedResult&p)->Result<std::string>{
     const std::string bridge_prefix="mosaic-to-p8-v1:";if(q.destination_id.rfind(bridge_prefix,0)!=0&&q.destination_id!="mosaic-texture-status-v1")return previous?previous(rt,q,p):Result<std::string>::failure({EOPNOTSUPP,"JOINT_BRIDGE","unregistered result destination"});
     if(!files->owns_runtime(&rt)||q.artifact||p.lease.app!=AppKind::NN)return Result<std::string>::failure({EXDEV,"JOINT_BRIDGE","same common original Mosaic result artifact0 required"});
     auto saved=exporter->export_result(p,"mosaic-file-v1");if(!saved)return Result<std::string>::failure(*saved.error);
     if(q.destination_id=="mosaic-texture-status-v1")return scene->mosaic_file_status(p.lease.producer);
     const auto suffix=q.destination_id.substr(bridge_prefix.size());if(suffix.size()!=64||!std::all_of(suffix.begin(),suffix.end(),[](char x){return(x>='0'&&x<='9')||(x>='a'&&x<='f');}))return Result<std::string>::failure({EINVAL,"JOINT_BRIDGE","independent trusted B conversion package digest required"});auto encoded=vxnn::json::parse("\""+suffix+"\"");
     auto bound=joint_texture_file(context,p,*saved.value,vxnn::json::digest(encoded.get()));if(!bound)return Result<std::string>::failure(*bound.error);
     return scene->accept_mosaic_file(*bound.value,p.lease.producer);
    };
   }
  }
  // No request START occurs during installation. Each NN preflight source was
  // dropped; media only registers its existing common worker/control hooks.
  construction.committed=true;return R::success(std::move(set));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"INSTALLATION","native registry allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"INSTALLATION",e.what()});}
};}
}
ApplicationBuilder installed_applications_from(const std::string&manifest,const std::string&sha_file){return installed_builder(manifest,sha_file,std::nullopt);}
ApplicationBuilder installed_applications_for_host_test(const std::string&manifest,const std::string&sha_file,const dp_ops&ops,std::shared_ptr<void>lifetime){return installed_builder(manifest,sha_file,HostDisplayBinding{ops,std::move(lifetime)});}
}
