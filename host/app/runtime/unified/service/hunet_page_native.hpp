// Included once inside NativeNnLibrary::State. All callbacks run on the existing
// execution owner; status reads only an atomic count and immutable metadata.
struct PageProgress {std::atomic<uint32_t>completed{0};uint32_t total=0;std::string page_id;};
using PageKey=std::tuple<std::string,std::string,std::string>;
std::map<PageKey,std::weak_ptr<PageProgress>>page_progress;
struct PageRun {
 std::shared_ptr<FixedCatalog>catalog;FixedPage page;
 std::shared_ptr<Model>model;std::shared_ptr<HostMemoryLease>charge;
 std::shared_ptr<NativeRequestFiles>files;std::shared_ptr<PageProgress>progress;
 std::shared_ptr<const std::vector<uint8_t>>source,rgb;
 std::vector<vxnn::RequestInput>words;
 std::vector<uint8_t>logits;std::vector<json::Ptr>rows,summary_rows;
 std::future<Result<std::shared_ptr<const vxnn::RequestInput>>>next_input;uint32_t next_input_index=0;bool prefetch=true;
};
static json::Ptr page_object(){auto*p=json_object_new_object();if(!p)throw std::bad_alloc();return json::Ptr(p,json_object_put);}
static json::Ptr page_array(){auto*p=json_object_new_array();if(!p)throw std::bad_alloc();return json::Ptr(p,json_object_put);}
static void page_put(json_object*o,const char*k,json_object*v){if(!v)throw std::bad_alloc();if(json_object_object_add(o,k,v)){json_object_put(v);throw std::bad_alloc();}}
static void page_text(json_object*o,const char*k,const std::string&v){page_put(o,k,json_object_new_string_len(v.data(),int(v.size())));}
static void page_num(json_object*o,const char*k,uint64_t v){page_put(o,k,json_object_new_uint64(v));}
static void page_push(json_object*a,json_object*v){if(!v)throw std::bad_alloc();if(json_object_array_add(a,v)){json_object_put(v);throw std::bad_alloc();}}
static std::vector<vxnn::RequestInput>page_words(const FixedCatalog&catalog,const FixedPage&page){
 std::vector<vxnn::RequestInput>words;words.reserve(page.items.size());
 for(auto i:page.items){if(i>=catalog.items.size())throw std::runtime_error("reviewed page item index");words.push_back(catalog.items[i].record);}
 return words;
}
static RegisteredInput page_registration(const InstalledNnModel&spec,const FixedCatalog&catalog,const FixedPage&page){
 auto words=page_words(catalog,page);auto input=hunet_page_input_id(words);auto plan=hunet_page_plan_id(spec.plan,words);
 if(!input||!plan)throw std::runtime_error("reviewed page metadata identity invalid");
 return {"reviewed-"+page.id,*input.value,words.size()*fixed_tensor_bytes,*plan.value};
}
static PageImage parse_page_image(json_object*page,const std::string&id,std::map<std::string,uint64_t>&members){
 PageImage out;auto*o=json::member(page,"image");json::fields(o,{"width","height","stride","source_file","source_bytes","source_format","rgb_file","rgb_bytes","rgb_sha256","conversion"});
 out.width=json::u32(json::member(o,"width"));out.height=json::u32(json::member(o,"height"));out.stride=json::u32(json::member(o,"stride"));
 out.source_file=json::str(json::member(o,"source_file"));out.rgb_file=json::str(json::member(o,"rgb_file"));out.source_format=json::str(json::member(o,"source_format"));
 out.source_bytes=json::u64(json::member(o,"source_bytes"));out.rgb_bytes=json::u64(json::member(o,"rgb_bytes"));out.rgb_sha=json::digest(json::member(o,"rgb_sha256"));
 json::check(out.width&&out.height&&out.width<=4096&&out.height<=4096&&out.stride==uint64_t(out.width)*3&&out.rgb_bytes==uint64_t(out.stride)*out.height&&out.rgb_bytes<=(16u<<20),"exact bounded original page RGB dimensions required");
 json::check(out.source_file==id+".source"&&out.rgb_file==id+".rgb"&&out.source_bytes&&out.source_bytes<=(16u<<20)&&nonzero_digest(out.rgb_sha)&&(out.source_format=="PNG"||out.source_format=="JPEG"||out.source_format=="WEBP")&&json::str(json::member(o,"conversion"))=="RGB_NO_GEOMETRY_CHANGE_V1","sealed page original and derived RGB identities required");
 json::check(members.emplace(out.source_file,out.source_bytes).second&&members.emplace(out.rgb_file,out.rgb_bytes).second,"duplicate original page member");return out;
}
Result<std::string>progress(const RequestKey&key){
 std::lock_guard<std::mutex>lock(mutex);auto i=page_progress.find({key.boot_id,key.client_id,key.request_id});auto observed=i==page_progress.end()?std::shared_ptr<PageProgress>{}:i->second.lock();if(!observed)return Result<std::string>::success("{}");
 auto o=page_object();page_text(o.get(),"kind","HUNET_REVIEWED_PAGE");page_text(o.get(),"page_id",observed->page_id);page_num(o.get(),"total_words",observed->total);page_num(o.get(),"completed_words",observed->completed.load(std::memory_order_acquire));return Result<std::string>::success(json::dump(o.get()));
}
Result<WorkRequest>prepare_page_session(const InstalledNnModel&spec,const RpcRequest&q,std::shared_ptr<FixedCatalog>catalog,const FixedPage&page){
 using R=Result<WorkRequest>;
 auto rt=runtime.lock();if(!rt)return R::failure({ESHUTDOWN,"HUNET_PAGE","Runtime expired"});
 if(page.items.empty()||page.items.size()>128||!page.image.width)return R::failure({EINVAL,"HUNET_PAGE","installed complete original page required"});
 const auto registered=page_registration(spec,*catalog,page);
 // Both the live CPU bytes and immutable tmpfs output quota are charged. No
 // Runtime calls are made by the input loader or the per-word commit callback.
 const uint64_t bundle_bytes=page.items.size()*32420ULL;
 const uint64_t host_bytes=json::add(2*fixed_tensor_bytes+(2u<<20),json::add(bundle_bytes,json::add(page.image.rgb_bytes,page.image.source_bytes)));
 auto charge=rt->reserve_host_memory(host_bytes);if(!charge)return R::failure(*charge.error);
 const uint64_t trace_bytes=json::add(65536,json::mul(page.items.size(),116ULL*4096+70000));
 auto quota=files->request(q.request,trace_bytes,bundle_bytes+page.image.rgb_bytes+page.image.source_bytes+(512u<<10));if(!quota)return R::failure(*quota.error);
 auto run=std::make_shared<PageRun>();run->catalog=std::move(catalog);run->page=page;auto active_model=load(spec);if(!active_model)return R::failure(*active_model.error);run->model=*active_model.value;run->charge=*charge.value;run->files=*quota.value;run->words=page_words(*run->catalog,page);run->logits.reserve(size_t(bundle_bytes));run->rows.reserve(run->words.size());run->summary_rows.reserve(run->words.size());
 auto original=fixed_read(run->catalog->directory->fd,page.image.source_file,16u<<20,page.image.source_bytes,page.source_sha);if(!original)return R::failure(*original.error);run->source=*original.value;
 auto rgb=fixed_read(run->catalog->directory->fd,page.image.rgb_file,16u<<20,page.image.rgb_bytes,page.image.rgb_sha);if(!rgb)return R::failure(*rgb.error);run->rgb=*rgb.value;
 run->progress=std::make_shared<PageProgress>();run->progress->total=uint32_t(run->words.size());run->progress->page_id=page.id;
 auto ns=router->allocate_cookie_namespace();if(!ns)return R::failure(*ns.error);
 run->prefetch=std::none_of(run->catalog->base.nodes.begin(),run->catalog->base.nodes.end(),[](const auto&n){return n.node.key.name=="Hunet.GnStatsCpuOrdered.v1";});
 auto read_input=[catalog=run->catalog,page=run->page](uint32_t index)->Result<std::shared_ptr<const vxnn::RequestInput>>{
  using L=Result<std::shared_ptr<const vxnn::RequestInput>>;if(index>=page.items.size())return L::failure({ERANGE,"HUNET_PAGE","word input index"});
  const auto&item=catalog->items.at(page.items[index]);
  // Read the pinned crop too: this is the exact reviewed source/preprocess,
  // not a regenerated crop or a new detector result.
  auto source=fixed_read(catalog->directory->fd,item.source_file,1u<<20,item.source_bytes,item.record.source_sha);if(!source)return L::failure(*source.error);
  static const uint8_t png[]={137,80,78,71,13,10,26,10};if((*source.value)->size()<8||std::memcmp((*source.value)->data(),png,8))return L::failure({EBADMSG,"HUNET_PAGE","fixed crop PNG header"});
  auto tensor=fixed_read(catalog->directory->fd,item.tensor_file,fixed_tensor_bytes,fixed_tensor_bytes,item.record.tensor_sha);if(!tensor)return L::failure(*tensor.error);
  for(size_t at=0;at<fixed_tensor_bytes;at+=4){float x;std::memcpy(&x,(*tensor.value)->data()+at,4);if(!std::isfinite(x)||x< -1.f||x>1.f)return L::failure({EDOM,"HUNET_PAGE","finite frozen P0 range required"});}
  auto input=std::make_shared<vxnn::RequestInput>(item.record);input->content=*tensor.value;return L::success(std::move(input));
 };
 HunetInputLoader loader=[run,read_input](uint32_t index)->Result<std::shared_ptr<const vxnn::RequestInput>>{
  using L=Result<std::shared_ptr<const vxnn::RequestInput>>;
  if(run->next_input.valid()&&run->next_input_index!=index)return L::failure({ESTALE,"HUNET_PAGE","single input prefetch order differs"});
  auto current=run->next_input.valid()?run->next_input.get():read_input(index);if(!current)return current;
  if(run->prefetch&&index+1<run->words.size()){
   run->next_input_index=index+1;run->next_input=std::async(std::launch::async,[read_input,index](){return read_input(index+1);});
  }
  return current;
 };
 HunetTraceSink trace=[run,trace_scope=scope](const RequestKey&,const HunetNodeTrace&t){
  auto raw=hunet_trace(t,trace_scope);auto tree=json::parse(raw);page_num(tree.get(),"word_index",t.word_index);page_num(tree.get(),"global_work_index",t.global_work_index);raw=json::dump(tree.get())+"\n";
  if(raw.size()>4096)return Result<void>::failure({EFBIG,"HUNET_PAGE","bounded per-node trace exceeded"});
  return run->files->append_trace({reinterpret_cast<const uint8_t*>(raw.data()),raw.size()},t.error!=0||t.node==116);
 };
 HunetWordSink commit=[run](const WorkRequest&w,const HunetWordIdentity&word,vxnn::Bytes b,const HunetRequestReceipt&)->Result<HunetWordCommit>{
  using C=Result<HunetWordCommit>;const auto i=word.word_index;
  if(i!=run->rows.size()||i>=run->words.size()||!b.data||b.size!=32420||word.input_id!=run->words[i].id||word.input_sha!=run->words[i].tensor_sha)return C::failure({ESTALE,"HUNET_PAGE","word output order/input/extent"});
  Digest sum{};vxl_hunet_hash(b.data,b.size,sum.data());
  std::vector<std::pair<float,uint32_t>>rank;rank.reserve(8105);for(uint32_t k=0;k<8105;k++){float f;std::memcpy(&f,b.data+4*k,4);if(!std::isfinite(f))return C::failure({EDOM,"HUNET_PAGE","nonfinite word logit"});rank.emplace_back(f,k);}
  std::partial_sort(rank.begin(),rank.begin()+5,rank.end(),[](const auto&a,const auto&b){return a.first>b.first||(a.first==b.first&&a.second<b.second);});
  auto row=page_object();auto brief=page_object();const auto&item=run->catalog->items[run->page.items[i]];
  for(auto*o:{row.get(),brief.get()}){page_num(o,"index",i);page_text(o,"input_id",word.input_id);page_text(o,"logits_sha256",vxnn::digest_hex(sum));auto ids=page_array(),scores=page_array();for(unsigned k=0;k<5;k++){page_push(ids.get(),json_object_new_uint64(rank[k].second));page_push(scores.get(),json_object_new_double(rank[k].first));}page_put(o,"topk_class_ids",json_object_get(ids.get()));page_put(o,"topk_raw_logits",json_object_get(scores.get()));}
  page_text(row.get(),"roi_id",item.roi_id);auto box=page_array();for(auto v:item.bbox)page_push(box.get(),json_object_new_uint64(v));page_put(row.get(),"bbox_xyxy",json_object_get(box.get()));
  page_text(row.get(),"source_sha256",vxnn::digest_hex(word.source_sha));page_text(row.get(),"tensor_sha256",vxnn::digest_hex(word.input_sha));page_text(row.get(),"preprocessing",run->words[i].preprocessing);page_text(row.get(),"preprocessing_id",vxnn::digest_hex(word.preprocessing_id));page_text(row.get(),"plan",vxnn::digest_hex(word.plan));page_num(row.get(),"offset",uint64_t(i)*32420);page_num(row.get(),"bytes",32420);
  const std::string reference="trace.raw#word-"+std::to_string(i);page_text(row.get(),"durable_record",reference);
  // Package canonical JSON forbids floats. This record is assembled only
  // from our own JSON object and hex bytes; do not reparse logits through the
  // package metadata parser or relax its separate numeric contract.
  const auto row_json=json::dump(row.get());
  const std::string encoded="{\"format\":\"hunet-page-word-durable-v1\",\"raw_logits_le_hex\":\""+hex(b.data,b.size)+"\","+row_json.substr(1)+"\n";if(encoded.size()>70000)return C::failure({EFBIG,"HUNET_PAGE","bounded durable word record"});
  auto saved=run->files->append_trace({reinterpret_cast<const uint8_t*>(encoded.data()),encoded.size()},true);if(!saved)return C::failure(*saved.error);
  // All capacity was reserved before execution; only after durable copy may
  // the owner replace this word's DATA/guard view with the following word.
  run->logits.insert(run->logits.end(),b.data,b.data+b.size);run->rows.push_back(std::move(row));run->summary_rows.push_back(std::move(brief));run->progress->completed.store(i+1,std::memory_order_release);
  HunetWordCommit out;out.producer=w.request;out.word=word;out.logits_sha=sum;out.logits_bytes=b.size;out.durable_record=reference;return C::success(std::move(out));
 };
 HunetPageOutputSink finish=[run,s=shared_from_this()](const WorkRequest&w,const std::vector<HunetWordCommit>&commits,const HunetRequestReceipt&receipt)->Result<PublishedResult>{
  using F=Result<PublishedResult>;if(commits.size()!=run->words.size()||run->rows.size()!=commits.size()||run->logits.size()!=commits.size()*32420ULL)return F::failure({EPROTO,"HUNET_PAGE","incomplete final page"});
  for(size_t i=0;i<commits.size();i++){Digest sum{};vxl_hunet_hash(run->logits.data()+i*32420,32420,sum.data());if(sum!=commits[i].logits_sha||commits[i].word.word_index!=i||commits[i].word.input_id!=run->words[i].id)return F::failure({EBADMSG,"HUNET_PAGE","durable word/bundle mapping differs"});}
  auto index=page_object();page_text(index.get(),"format","hunet-page-result-v1");page_text(index.get(),"page_id",run->page.id);page_text(index.get(),"title",run->page.title);page_text(index.get(),"catalog_sha256",vxnn::digest_hex(run->catalog->manifest));page_text(index.get(),"source_sha256",vxnn::digest_hex(run->page.source_sha));page_num(index.get(),"width",run->page.image.width);page_num(index.get(),"height",run->page.image.height);page_text(index.get(),"rgb_sha256",vxnn::digest_hex(run->page.image.rgb_sha));page_text(index.get(),"model",vxnn::digest_hex(w.model));page_text(index.get(),"base_plan",vxnn::digest_hex(run->catalog->base.plan_id));page_text(index.get(),"plan",vxnn::digest_hex(w.plan));page_text(index.get(),"input_sha256",vxnn::digest_hex(w.input_sha));page_num(index.get(),"total",run->words.size());page_num(index.get(),"completed",commits.size());page_text(index.get(),"order","REVIEWED_MANIFEST_ORDER");page_text(index.get(),"score_kind","RAW_LOGIT_NOT_PROBABILITY");page_text(index.get(),"numeric_class","NOT_EVALUATED");
  auto rows=page_array();for(const auto&row:run->rows)page_push(rows.get(),json_object_get(row.get()));page_put(index.get(),"words",json_object_get(rows.get()));
  auto performance=page_object();page_num(performance.get(),"constant_uploads",receipt.constant_uploads);page_num(performance.get(),"constant_upload_bytes",receipt.constant_upload_bytes);page_num(performance.get(),"input_uploads",receipt.input_uploads);page_num(performance.get(),"input_upload_bytes",receipt.input_upload_bytes);page_num(performance.get(),"data_allocations",receipt.data_allocations);page_num(performance.get(),"args_allocations",receipt.args_allocations);page_num(performance.get(),"words_completed",receipt.words_completed);if(receipt.gpu_raw_cycles)page_num(performance.get(),"gpu_raw_cycles",*receipt.gpu_raw_cycles);else if(json_object_object_add(performance.get(),"gpu_raw_cycles",nullptr))throw std::bad_alloc();page_put(index.get(),"performance",json_object_get(performance.get()));
  auto encoded=json::dump(index.get())+"\n";if(encoded.size()>(512u<<10))return F::failure({EFBIG,"HUNET_PAGE","bounded full page index"});
  auto bundle=run->files->output("page-logits.f32",{run->logits.data(),run->logits.size()},"HUNET_PAGE_F32",{commits.size(),8105,1});if(!bundle)return F::failure(*bundle.error);
  auto metadata=run->files->output("page-result.json",{reinterpret_cast<const uint8_t*>(encoded.data()),encoded.size()},"JSON_UTF8",{encoded.size()});if(!metadata)return F::failure(*metadata.error);
  auto rgb=run->files->output("page-source.rgb",{run->rgb->data(),run->rgb->size()},"RGB888",{run->page.image.height,run->page.image.width,3});if(!rgb)return F::failure(*rgb.error);
  auto original=run->files->output("page-original.source",{run->source->data(),run->source->size()},"OPAQUE_U8",{run->source->size()});if(!original)return F::failure(*original.error);
  PublishedResult out;out.execution_complete=true;out.lease.producer=w.request;out.lease.app=AppKind::NN;out.lease.scope=s->scope;out.lease.hardware=s->hw.hardware_contract;out.lease.model=w.model;out.lease.plan=w.plan;out.lease.artifacts={*bundle.value,*metadata.value,*rgb.value,*original.value};
  auto summary=page_object();page_text(summary.get(),"format","native-hunet-page-result-v1");page_text(summary.get(),"page_id",run->page.id);page_num(summary.get(),"total",commits.size());page_num(summary.get(),"completed",commits.size());page_text(summary.get(),"order","REVIEWED_MANIFEST_ORDER");page_text(summary.get(),"score_kind","RAW_LOGIT_NOT_PROBABILITY");auto brief=page_array();for(const auto&row:run->summary_rows)page_push(brief.get(),json_object_get(row.get()));page_put(summary.get(),"words",json_object_get(brief.get()));page_put(summary.get(),"performance",json_object_get(performance.get()));out.raw_legacy_json=json::dump(summary.get());if(out.raw_legacy_json.size()>65536)return F::failure({EFBIG,"HUNET_PAGE","bounded RPC page summary"});return F::success(std::move(out));
 };
 auto app=HunetRequest::prepare_page(run->model->package,std::get<vxnn::HunetFamily>(spec.family),caps,hw,q.request,residency,scope,*ns.value,spec.job_budget_ns,run->words,std::move(loader),std::move(trace),std::move(commit),std::move(finish));if(!app)return R::failure(*app.error);
 auto request=(*app.value)->request();if(request.model!=spec.model||request.plan!=registered.plan||request.input_sha!=registered.sha||request.input_bytes!=registered.bytes||request.source->size()!=run->words.size()*116)return R::failure({ESTALE,"HUNET_PAGE","page identity/work count differs admission"});
 auto bound=router->bind_source(std::move(request),(*app.value)->hooks(),run->model->charge,run);if(!bound)return bound;
 {std::lock_guard<std::mutex>lock(mutex);for(auto it=page_progress.begin();it!=page_progress.end();)if(it->second.expired())it=page_progress.erase(it);else ++it;if(page_progress.size()>=256)return R::failure({ENOSPC,"HUNET_PAGE","page progress registry256"});if(!page_progress.emplace(PageKey{q.request.boot_id,q.request.client_id,q.request.request_id},run->progress).second)return R::failure({EEXIST,"HUNET_PAGE","page progress identity retained"});}
 return bound;
}
