"""A/RY R13 offline FIT/rootfs packer. --prepare never creates image/release.
Use --run only after the parent's final cross/model green notice. No mounts,
media/device access, target ELF execution, SSH or private-key reads.
"""
import argparse,ast,copy,datetime,hashlib,importlib.util,json,os,pathlib,re,resource,stat,struct,subprocess,sys,time,uuid
sys.dont_write_bytecode=True
os.environ['PYTHONDONTWRITEBYTECODE']='1'
P=pathlib.Path
GROOT=P('/mnt/g/s8_software_r5_20261028')
ROOT=GROOT/'software-r13-performance-633/source'
TASK=GROOT/'software-r13-performance-633/image-packer-r13-671'
ORCH=P('/mnt/f/obsidian-codex/codex/work/s8_300s_optimization_20261026')
HELPER=ORCH/'a_r4_image_repack_252.py'
HELPER_SHA='2ce22c62cb3c1228a77659a27cd5fcef1854ea5e1cbe3725683e1018168bfb83'
BASE_R3=GROOT/'archive-stage-406/frozen-base-r3'
OLD_SOURCE=GROOT/'archive-stage-406/source'
KERNEL=GROOT/'archive-stage-406/native/kernel'
KERNEL_IMAGE_SHA='b800b5a6da026425c3e3ecf3f67c4914bc89acf7691ad6772911ef889ebb0331'
SOURCE_LOCK=ROOT/'build/source-lock-r13-668.json'
CROSS_RECEIPT=GROOT/'software-r13-performance-633/runtime-audit-r5-r13-669/receipt.json'
MODEL_RECEIPT=GROOT/'software-r13-performance-633/evidence/a-r13-models-host-668/receipt.json'
MODELS=GROOT/'software-r13-performance-633/models-final-r13-668'
PRESETS=GROOT/'hunet-page-assets-525/payload'
PRESET_SHA='af055aa4c09f41e57a4c4edb6a7ef106582350120c5b898251a3cbd4c09f9a12'
PRESET_TARGET='/usr/share/vxgpu/hunet-fixed-inputs'
HOST_PARSER=GROOT/'software-r13-performance-633/host-final-r13-668/out/vxgpud'
WORK=GROOT/'software-r13-performance-633/image-stage-r13-671a'
RELEASE=GROOT/'software-r13-performance-633/delivery/release-r13-671'
PUBLIC_KEY=P('/mnt/c/Users/13151/.ssh/id_rsa_codex_z11p_20261007.pub')
EXPECTED_NAMES=('hunet','hunet-gn9','hunet-gn16','mosaic','mosaic-profile','mosaic-smoke','mosaic-smoke-profile','mosaic-b1024','mosaic-b4096')
ALL_GN=(2,9,19,25,32,42,52,55,62,69,76,83,90,97,100,107)
BUDGET=50000000000
_runtime=None
CAPACITY_HELPER_SHA='7c0b0898706b1ed6c85e80be8b2d9ee5123080c643de296c12a8de7563487ca5'

def need(ok,message):
 if not ok:raise ValueError(message)

def digest(raw):return hashlib.sha256(raw).hexdigest()
def wire(obj):return (json.dumps(obj,ensure_ascii=False,sort_keys=True,indent=2)+'\n').encode()
def canonical(obj):return json.dumps(obj,ensure_ascii=False,sort_keys=True,separators=(',',':'))
def evidence(raw):return dict(bytes=len(raw),sha256=digest(raw))
def regular(path,maximum=512<<20,allow_empty=False):
 path=P(path);a=path.lstat();need(stat.S_ISREG(a.st_mode) and (a.st_size>=0 if allow_empty else a.st_size>0) and a.st_size<=maximum,'bounded regular input required: '+str(path));raw=path.read_bytes();b=path.lstat();need((a.st_dev,a.st_ino,a.st_size,a.st_mtime_ns)==(b.st_dev,b.st_ino,b.st_size,b.st_mtime_ns) and len(raw)==a.st_size,'input changed while reading: '+str(path));return raw

def file_ref(path,maximum=512<<20):
 path=P(path);a=path.lstat();need(stat.S_ISREG(a.st_mode) and 0<a.st_size<=maximum,'bounded regular artifact required: '+str(path));h=hashlib.sha256()
 with path.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 z=path.lstat();need((a.st_dev,a.st_ino,a.st_size,a.st_mtime_ns)==(z.st_dev,z.st_ino,z.st_size,z.st_mtime_ns),'artifact changed during hash');return dict(path=str(path),bytes=a.st_size,sha256=h.hexdigest())

def unique_json(raw):
 def unique(pairs):
  out={}
  for k,v in pairs:need(k not in out,'duplicate JSON key '+k);out[k]=v
  return out
 return json.loads(raw.decode('utf8'),object_pairs_hook=unique,parse_constant=lambda x:(_ for _ in ()).throw(ValueError('nonfinite JSON constant '+x)))

def json_file(path,maximum=16<<20):return unique_json(regular(path,maximum))
def is_digest(value):return type(value) is str and re.fullmatch('[0-9a-f]{64}',value) is not None and value!='0'*64

def evidence_path(label):
 need(type(label) is str and re.fullmatch('[A-Za-z0-9_-]{1,64}',label) is not None,'bounded fresh evidence label required');p=TASK/'evidence'/label;need(not p.exists() and not p.is_symlink(),'evidence run already exists; preserve it');return p

def load_runtime():
 global _runtime
 if _runtime is not None:return _runtime
 need(file_ref(HELPER)['sha256']==HELPER_SHA,'frozen252 helper changed')
 saved=list(sys.path);sys.path[:0]=[str(ROOT),str(ROOT/'scripts/cnn')]
 from scripts.unified import audit_image,audit_kernel,cross_common,bind_kernel
 import image_audit as cpio_parser
 for module in [audit_image,audit_kernel,cross_common,bind_kernel,cpio_parser]:need(P(module.__file__).resolve().is_relative_to(ROOT),'only actual R10 auditor/CPIO modules may be imported')
 spec=importlib.util.spec_from_file_location('frozen252_byte_utilities',HELPER);repack=importlib.util.module_from_spec(spec);spec.loader.exec_module(repack);sys.path[:]=saved
 # Input-location substitution only; baseline() retains every original PINS gate.
 repack.RELEASE=BASE_R3
 need(P(repack._cpio.__file__).resolve().is_relative_to(ROOT),'frozen helper CPIO dependency must be current source')
 _runtime=dict(repack=repack,audit_image=audit_image,audit_kernel=audit_kernel,cross_common=cross_common,bind_kernel=bind_kernel)
 globals().update(member_name=repack.member_name,newc_records=repack.newc_records,cpio_entries=repack.cpio_entries,normalize_cpio_entries=audit_image.normalize_cpio_entries,payload_errors=audit_image.payload_errors,client_runtime_errors=audit_image.client_runtime_errors)
 return _runtime

def validate_model_rows(document):
 need(type(document) is dict and type(document.get('natural_exit')) is int and document['natural_exit']==0,'successful genuine model preparation receipt required')
 rows=document.get('models');need(type(rows) is list and len(rows)==9,'exact nine matching model rows required');out={}
 for row in rows:
  need(type(row) is dict and set(row)=={'name','package','admission'},'exact name/package/admission model row required');name=row['name'];need(type(name) is str and name in EXPECTED_NAMES and name not in out,'distinct known model ID required');need(row['package']==str(MODELS/name),'exact owned model package source required')
  a=row['admission'];need(type(a) is dict and a.get('scope')=='ACTUAL_HOST_PACKAGE_PREPARATION_DECLARED_CAPS_NOT_DEVICE_OR_GPU' and a.get('gpu_raw_cycles','missing') is None and a.get('board_identity_verified') is False,'actual HOST metadata admission scope required')
  m=a.get('installed_model');kind='hunet' if name.startswith('hunet') else 'mosaic';smoke='smoke' in name;need(type(m) is dict and m.get('id')==name and m.get('kind')==kind and m.get('package')=='/usr/share/vxgpu/models/'+name,'original explicit metadata ID/kind/target must match without rewrite')
  wanted_bytes=196608 if kind=='hunet' else 1152 if smoke else 14745600;wanted_input='frozen-roi0' if kind=='hunet' else 'frozen-12x8' if smoke else 'frozen-1280x960';need(type(m.get('input_bytes')) is int and m['input_bytes']==wanted_bytes and m.get('input_id')==wanted_input,'actual frozen input geometry label/bytes differ')
  need(all(is_digest(m.get(k)) for k in ['model','plan','input']),'actual admitted model/plan/input digest required');need(all(type(m.get(k)) is int and m[k]>0 for k in ['resident_bytes','job_budget_ns']),'typed positive model storage/budget required');family=m.get('family');need(type(family) is dict and all(is_digest(family.get(k)) for k in ['code','schema','implementation']+(['readonly'] if kind=='mosaic' else [])),'complete actual model family identity required')
  if kind=='hunet':need(m.get('presets')==dict(directory=PRESET_TARGET,sha256=PRESET_SHA),'matching HOST row must explicitly declare unchanged525 presets')
  else:need('presets' not in m,'Mosaic cannot borrow HUNet presets')
  out[name]=copy.deepcopy(m)
 need(set(out)==set(EXPECTED_NAMES),'exact complete nine-ID set required');return out

def validate_variant_policy(name,binding):
 need(name in EXPECTED_NAMES and type(binding) is dict,'explicit known package policy required')
 if name.startswith('hunet'):
  want=[] if name=='hunet' else [9] if name=='hunet-gn9' else list(ALL_GN);got=binding.get('cpu_gn_stats_nodes');need(type(got) is list and all(type(n) is int for n in got) and got==want,'exact defaultGPU/node9/all16 GN policy required');return None
 smoke='smoke' in name;conv=64 if smoke else 1024 if name=='mosaic-b1024' else 4096 if name=='mosaic-b4096' else 256;profile=int('profile' in name);batch=binding.get('batch_policy',{});workspace=binding.get('workspace_policy',{});want=dict(width=12 if smoke else 1280,height=8 if smoke else 960,budget=conv)
 need(all(type(binding.get(k)) is int and binding[k]==v for k,v in want.items()),'exact full/smoke geometry and convolution budget required');need(all(type(batch.get(k)) is int and batch[k]==v for k,v in dict(version=1,convolution_budget=conv,pointwise_budget=64 if smoke else 4096).items()),'explicit split batching policy required');need(all(type(workspace.get(k)) is int and workspace[k]==v for k,v in dict(abi=4,schedule_version=2,weight_layout_version=2,profiling_options=profile).items()),'explicit reuse-v8 ABI4/schedule2/weights2/profile policy required');return None

def network_payload(raw):
 need(type(raw) is bytes,'actual network-init bytes required');raw=raw.replace(b'\r\n',b'\n');needle=b'  ip link set dev eth0 up || exit $?\n';need(raw.count(needle)==1 and b'10.0.0.10/24' not in raw,'unambiguous original primary-only network script required');return raw.replace(needle,needle+b"  if ! ip -4 addr show dev eth0 | grep -q 'inet 10.0.0.10/24'; then\n   ip addr add 10.0.0.10/24 dev eth0 || exit $?\n  fi\n")

def rootfs_policy_projection_valid():
 need(file_ref(HELPER)['sha256']==HELPER_SHA,'frozen helper changed');old=next(n for n in ast.parse(regular(HELPER).decode()).body if isinstance(n,ast.FunctionDef) and n.name=='audit_rootfs');new=next(n for n in ast.parse(P(__file__).read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='audit_rootfs_r10');old.name='audit_rootfs_r10'
 class Adapt(ast.NodeTransformer):
  def visit_Set(self,node):
   if {n.value for n in node.elts if isinstance(n,ast.Constant)}=={'hunet','mosaic'} and len(node.elts)==2:return ast.Call(func=ast.Name(id='set',ctx=ast.Load()),args=[ast.Name(id='EXPECTED_NAMES',ctx=ast.Load())],keywords=[])
   return self.generic_visit(node)
  def visit_Constant(self,node):return ast.Constant('exact nine NN installations') if node.value=='exact two NN installations' else node
 old=Adapt().visit(old);return ast.dump(old,include_attributes=False)==ast.dump(new,include_attributes=False)

def audit_rootfs_r10(raw_cpio,build_id,cross_receipt=None,installed_models=None):
    """Re-audit actual archive bytes, captured links, packages, and target runtime.

    installed_models is {id: installed_model} from the genuine matching HOST
    preparation probe. Its equality is checked, never a declaration of GPU PASS.
    A missing cross/probe is a listed pending gate, not a fabricated acceptance.
    """
    newc_records(raw_cpio);entries=cpio_entries(raw_cpio);normalized=normalize_cpio_entries(entries)
    raw=lambda n:entries.get(n,{}).get('data',b'')
    manifest=json.loads(raw('usr/share/vxgpu/payload-manifest.json'));need(manifest['build_id']==build_id,'rootfs payload build ID differs')
    errors=payload_errors(normalized,manifest);need(not errors,'actual original payload admission: '+repr(errors))
    installation=json.loads(raw('etc/vxgpud/installation.json'));declared={r['id']:r for r in installation['nn']}
    need(set(declared)==set(EXPECTED_NAMES),'exact nine NN installations')
    packages={}
    for name,row in declared.items():
        prefix='usr/share/vxgpu/models/'+name
        need(row['package']=='/'+prefix,'bounded exact installed model directory')
        package=json.loads(raw(prefix+'/manifest.json'));sealed=copy.deepcopy(package);model_id=sealed.pop('model_id')
        need(digest(json.dumps(sealed,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode())==model_id==row['model'],'actual installed sealed model ID')
        names={prefix+'/manifest.json'}
        for asset in package['assets']:
            member_name(asset['path']);path=prefix+'/'+asset['path'];need(path not in names,'unique model asset path');names.add(path)
            need(evidence(raw(path))=={k:asset[k] for k in ('bytes','sha256')},'actual package asset differs: '+path)
        need({n for n,v in entries.items() if n.startswith(prefix+'/') and v['kind']!=stat.S_IFDIR}==names,'exact model member inventory; no stale/link/extra assets')
        packages[name]=dict(model=model_id,members=len(names),installed_model=row)
    sys.path.insert(0,str(ROOT/'scripts/hunet'))
    from scripts.hunet import image_audit as legacy
    resolved=legacy.resolved_regular_files(entries)
    errors=client_runtime_errors(resolved);need(not errors,'actual Python control-client/runtime closure: '+repr(errors))
    if cross_receipt is not None:
        from scripts.unified.cross_common import snapshot, aarch64_target
        from scripts.unified.bind_kernel import require_cross_inputs
        require_cross_inputs(cross_receipt,snapshot(ROOT),aarch64_target(4))
        for row in cross_receipt['target_runtime']['files'].values():
            data=resolved.get(row['path'],b'')
            need(digest(data)==row['sha256'] and data[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(data[18:20],'little')==183,'actual matched AArch64 runtime closure: '+row['path'])
        for row in cross_receipt['installation']:
            need(row['destination'] in ('/usr/bin/vxgpud','/usr/bin/vxgpuctl'),'only production daemon/client install')
            data=resolved.get(row['destination'][1:],b'');need(digest(data)==row['sha256'],'actual installed cross artifact differs')
            if row['destination']=='/usr/bin/vxgpud':need(data[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(data[18:20],'little')==183,'real production AArch64 daemon required')
    if installed_models is not None:need(installed_models==declared,'actual rootfs installation differs from matching HOST model admission')
    return dict(scope='ACTUAL_CPIO_PAYLOAD_MODEL_ASSETS_CLIENT_RUNTIME_BYTES_NOT_BOARD',build_id=build_id,payload_manifest=evidence(raw('usr/share/vxgpu/payload-manifest.json')),packages=packages,cross_receipt_checked=cross_receipt is not None,matching_HOST_model_admission_checked=installed_models is not None,pending_gates=(["MATCHING_AARCH64_CROSS_AND_SOURCE_LOCK"] if cross_receipt is None else [])+(["MATCHING_HOST_MODEL_GRAPH_PLAN_ADMISSION"] if installed_models is None else []),gpu_raw_cycles=None,ready_for_sd=False)


def existing_removal_prefixes(entries,prefixes):
 need(type(entries) is dict and type(prefixes) is list and len(prefixes)==len(set(prefixes)),'unique explicit prefix inventory required')
 present=[];absent=[]
 for prefix in prefixes:
  load_runtime()['repack'].member_name(prefix)
  if prefix in entries:
   need(entries[prefix].get('kind')==stat.S_IFDIR,'existing replacement prefix must remain a real directory');present.append(prefix)
  else:
   need(not any(n.startswith(prefix+'/') for n in entries),'absent prefix cannot hide existing descendants');absent.append(prefix)
 return present,absent

def inventory(directory):
 directory=P(directory);need(directory.is_dir() and not directory.is_symlink(),'real package directory required');files={}
 for folder,dirs,names in os.walk(directory,followlinks=False):
  for d in dirs:need(not P(folder,d).is_symlink(),'source directory symlink forbidden')
  for name in names:
   p=P(folder,name);need(stat.S_ISREG(p.lstat().st_mode),'regular package source required');rel=p.relative_to(directory).as_posix();load_runtime()['repack'].member_name(rel);files[rel]=file_ref(p)
 return files

def presets_audit():
 rows=inventory(PRESETS);need(set(rows)>={'manifest.json','manifest.sha256'} and rows['manifest.json']['sha256']==PRESET_SHA,'exact525 v2 catalog pin required');raw=regular(PRESETS/'manifest.json',256<<10);doc=unique_json(raw);need(regular(PRESETS/'manifest.sha256',65)==(PRESET_SHA+'\n').encode(),'actual525 seal differs');need(doc.get('format')=='hunet-fixed-inputs-v2' and doc.get('version')==2 and doc.get('preprocessing')=='HUNET_REVIEWED_P0_FROZEN_V1','actual full-page v2 policy required');need(doc.get('preprocessing_id')==digest(doc['preprocessing'].encode()),'actual preprocessing identity required');pages=doc.get('pages');need(type(pages) is list and [p.get('id') for p in pages]==['page1','page2','page3'] and [len(p.get('items',[])) for p in pages]==[20,13,88],'original reviewed121-word order required');members={'manifest.json','manifest.sha256'}
 for page,(width,height) in zip(pages,[(474,345),(1080,1492),(420,655)]):
  im=page['image'];need(im['width']==width and im['height']==height and im['stride']==3*width and im['rgb_bytes']==3*width*height and im['conversion']=='RGB_NO_GEOMETRY_CHANGE_V1','original full page geometry/conversion differs')
  for leaf,size,want in [(im['source_file'],im['source_bytes'],page['source_sha256']),(im['rgb_file'],im['rgb_bytes'],im['rgb_sha256'])]:need(leaf in rows and rows[leaf]['bytes']==size and rows[leaf]['sha256']==want,'actual original page source/RGB differs');members.add(leaf)
  for item in page['items']:
   for role in ['source','tensor']:
    leaf=item[role+'_file'];need(leaf in rows and rows[leaf]['bytes']==item[role+'_bytes'] and rows[leaf]['sha256']==item[role+'_sha256'],'actual reviewed crop/tensor differs');members.add(leaf)
 need(set(rows)==members and len(rows)==250,'exact250 preset members required');return dict(scope='ACTUAL_525_CONTENT_NOT_INFERENCE',manifest_sha256=PRESET_SHA,pages=[20,13,88],total=121,files=rows)

def kernel_reuse_audit():
 comparisons={};total=0
 for sub in ['driver','core','include/uapi','include/vxl']:
  old={p.relative_to(OLD_SOURCE).as_posix():file_ref(p) for p in (OLD_SOURCE/sub).rglob('*') if p.is_file()};new={p.relative_to(ROOT).as_posix():file_ref(p) for p in (ROOT/sub).rglob('*') if p.is_file()};need(set(old)==set(new),'kernel input member set differs: '+sub);need(all(old[n]['sha256']==new[n]['sha256'] and old[n]['bytes']==new[n]['bytes'] for n in old),'actual driver/core/UAPI/VXL bytes differ: '+sub);comparisons[sub]=new;total+=len(new)
 image=regular(KERNEL/'Image');need(len(image)==23953920 and digest(image)==KERNEL_IMAGE_SHA and image[56:60]==b'ARMd','frozen actual A kernel Image identity differs');config=regular(KERNEL/'kernel.config',1<<20);need(b'CONFIG_VXL_LINUX=y\n' in config,'actual built-in kernel configuration required');vm=file_ref(KERNEL/'vmlinux');head=(KERNEL/'vmlinux').open('rb');elf=head.read(64);head.close();need(elf[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(elf[18:20],'little')==183,'actual archived ARM64 vmlinux required');system=regular(KERNEL/'System.map',8<<20);need(b'vxl_' in system,'actual built-in driver symbols required')
 return dict(scope='FROZEN_A_KERNEL199_INPUTS_IDENTICAL_NO_REBUILD_NOT_BOOT',no_rebuild=True,compared_files=total,comparison=comparisons,artifacts=dict(Image=evidence(image),config=evidence(config),vmlinux=vm,System_map=evidence(system)))

def package_audit(installed,firmware):
 rt=load_runtime();out={};snapshot_rows={r['path']:r for r in rt['cross_common'].snapshot(ROOT)['files']};provider=rt['cross_common'].provider(ROOT,4)
 for name in EXPECTED_NAMES:
  folder=MODELS/name;rows=inventory(folder);need('manifest.json' in rows,'model manifest required');doc=json_file(folder/'manifest.json');sealed=copy.deepcopy(doc);model_id=sealed.pop('model_id');need(digest(canonical(sealed).encode())==model_id==installed[name]['model'],'actual sealed package model ID differs metadata');members={'manifest.json'}
  for asset in doc['assets']:
   leaf=asset['path'];rt['repack'].member_name(leaf);need(leaf not in members and leaf in rows and rows[leaf]['bytes']==asset['bytes'] and rows[leaf]['sha256']==asset['sha256'],'actual model asset inventory/bytes/hash differs');members.add(leaf)
  need(set(rows)==members,'no stale/link/extra model members');binding=json_file(folder/'binding.json');validate_variant_policy(name,binding);need(binding.get('hardware')==provider,'actual model provider differs final hardware contract');need(binding.get('implementation_sha256')==installed[name]['family']['implementation'],'actual implementation differs HOST admission');need(rows['input.f32']['sha256']==installed[name]['input'] and rows['input.f32']['bytes']==installed[name]['input_bytes'],'actual frozen input differs HOST row')
  sources=binding.get('source_rows');need(type(sources) is list and sources and len({r['path'] for r in sources})==len(sources),'actual unique model runtime closure required')
  for row in sources:
   rt['repack'].member_name(row['path']);actual=snapshot_rows.get(row['path']);need(actual is not None and actual['sha256']==row['sha256'],'package source differs final locked source: '+row['path'])
   if 'newline' in row:need(row['newline']==('CRLF_RAW' if b'\r\n' in regular(ROOT/row['path']) else 'LF_RAW'),'source newline identity differs')
  need(digest(canonical(sorted(sources,key=lambda x:x['path'])).encode())==binding['implementation_sha256'],'actual implementation/source rows identity differs')
  if name.startswith('mosaic'):family=rt['bind_kernel'].verify_mosaic_firmware_binding(ROOT,firmware,folder,mosaic_variant='reuse-v8')
  else:
   entry=next(e for e in firmware['entries'] if (e['namespace'],e['logical_profile'],e['logical_program'])==(1,1,65552));need(binding['code_sha256']==installed[name]['family']['code'] and binding['schema_sha256']==installed[name]['family']['schema'] and digest(entry['payload'])==binding['code_sha256'] and digest(entry['schema'])==binding['schema_sha256'],'actual HUNet firmware/model code/schema differs');family=dict(code_sha256=binding['code_sha256'],schema_sha256=binding['schema_sha256'])
  out[name]=dict(model=model_id,files=rows,family=family)
 return out

def task_logical_bytes():
 helper=ORCH/'count_r13_capacity_668.py'
 need(file_ref(helper)['sha256']==CAPACITY_HELPER_SHA,'native incremental upper-bound helper changed')
 q=subprocess.run(['/mnt/d/PYTHON/python.exe','-B','-X','utf8',str(helper).replace('/mnt/f/','F:/')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=45)
 need(q.returncode==0,'native capacity command failed: '+q.stderr.decode('utf8','replace')[-1000:]);doc=unique_json(q.stdout)
 need(doc.get('scope')=='NATIVE_CONSERVATIVE_BASELINE_PLUS_ENTIRE_R13_SUBTREE' and doc.get('baseline_bytes')==43437066041 and doc.get('no_subtraction') is True,'exact immutable baseline plus entire new subtree required')
 need(type(doc.get('logical_bytes_upper_bound')) is int and doc['logical_bytes_upper_bound']<BUDGET,'G task conservative50GB cap')
 folder=TASK/'capacity';folder.mkdir(exist_ok=True);raw=wire(doc);path=folder/('capacity-'+str(time.time_ns())+'.json')
 with path.open('xb') as f:need(f.write(raw)==len(raw),'capacity receipt write');f.flush();os.fsync(f.fileno())
 return doc['logical_bytes_upper_bound']+len(raw)


def host_config_check(raw,ev):
 ref=file_ref(HOST_PARSER);head=HOST_PARSER.open('rb');ident=head.read(64);head.close();need(ident[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(ident[18:20],'little')==62,'only actual HOST x86_64 parser may execute');root=P('/dev/shm/s8_image538_'+uuid.uuid4().hex);need(not root.exists() and not root.is_symlink(),'unique RAM config container required');root.mkdir(mode=0o700);config=root/'config.json';record=dict(scope='HOST_OFFLINE_CONFIG_PARSER_ONLY_NOT_TARGET',parser=ref,config_sha256=digest(raw),root=str(root),absent_before=True,uid=os.geteuid(),root_mode='0700',file_mode='0600',natural_exit=None)
 try:
  fd=os.open(config,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
  with os.fdopen(fd,'wb') as f:need(f.write(raw)==len(raw),'config write complete');f.flush();os.fsync(f.fileno())
  for p,mode in [(root,0o700),(config,0o600)]:st=p.lstat();need(st.st_uid==os.geteuid() and stat.S_IMODE(st.st_mode)==mode,'exact same-uid private config modes required')
  need(regular(config)==raw,'RAM config copy SHA differs');argv=[str(HOST_PARSER),'--check-config',str(config)];result=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE);(ev/'host-config.stdout').write_bytes(result.stdout);(ev/'host-config.stderr').write_bytes(result.stderr);record.update(argv=argv,natural_exit=result.returncode);need(result.returncode==0,'HOST parser rejected config; logs retained');doc=unique_json(result.stdout);need(doc.get('scope')=='OFFLINE_CONFIG_VALIDATION' and doc.get('device_opened') is False and doc.get('gpu_raw_cycles','missing') is None and doc.get('config_sha256')==digest(raw),'original HOST parser scope/SHA differs');need(regular(config)==raw and set(root.iterdir())=={config},'HOST config changed or unexpected RAM files')
 finally:
  (ev/'host-config.json').write_bytes(wire(record))
  if config.exists():config.unlink()
  root.rmdir();record['cleaned']=not root.exists();(ev/'host-config.json').write_bytes(wire(record))
 return record

def host_installation_check(raw,ev):
 probe=GROOT/'software-r13-performance-633/installation-decoder-r13-670/out/decoder-r13';proof_path=GROOT/'software-r13-performance-633/installation-decoder-r13-670/receipt.json';proof=json_file(proof_path);locked=json_file(SOURCE_LOCK)['snapshot']
 need(proof.get('natural_exit')==0 and proof.get('gap_reproduced') is True,'real old/new decoder gap proof required');need(proof['source_snapshot_id']==locked['source_snapshot_id'] and proof['runtime_source_id']==locked['runtime_source_id'],'installation decoder source differs final lock');ref=file_ref(probe);need(ref['sha256']==proof['decoder']['sha256'],'installation decoder executable changed')
 head=probe.open('rb');ident=head.read(64);head.close();need(ident[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(ident[18:20],'little')==62,'HOST decoder required; never execute target ELF')
 root=P('/dev/shm/s8_r13_install_'+uuid.uuid4().hex);need(not root.exists() and not root.is_symlink(),'fresh RAM installation input');root.mkdir(mode=0o700);input_file=root/'installation.json';record=dict(scope='ACTUAL_CPIO_INSTALLATION_HOST_DECODER_NOT_DEVICE_GPU_DP',decoder=ref,proof=file_ref(proof_path),input_sha256=digest(raw),device_opened=False)
 try:
  with input_file.open('xb') as f:need(f.write(raw)==len(raw),'whole installation probe input');f.flush();os.fsync(f.fileno())
  input_file.chmod(0o400);q=subprocess.run([str(probe),str(input_file)],stdout=subprocess.PIPE,stderr=subprocess.PIPE);(ev/'host-installation.stdout').write_bytes(q.stdout);(ev/'host-installation.stderr').write_bytes(q.stderr);record['natural_exit']=q.returncode;need(q.returncode==0,'actual CPIO installation rejected by production decoder');doc=unique_json(q.stdout);need(doc.get('scope')=='HOST_INSTALLATION_DECODER_ONLY_NOT_DEVICE_GPU_DP' and doc.get('device_opened') is False and doc.get('version')==2 and doc.get('nn_count')==9 and doc.get('ids')==list(EXPECTED_NAMES) and doc.get('input_sha256')==digest(raw),'actual nine-entry decoder result/bytes differ');need(regular(input_file)==raw,'decoder modified input');record['result']=doc;return record
 finally:
  if input_file.exists():input_file.chmod(0o600);input_file.unlink()
  root.rmdir();record['ram_input_removed']=not root.exists();(ev/'host-installation.json').write_bytes(wire(record))

def preflight(source_lock_sha=None,cross_sha=None,models_sha=None):
 rt=load_runtime();need(rootfs_policy_projection_valid(),'local rootfs audit diverged from frozen source');pending=[];rec=dict(scope='PREPARATION_ONLY_NOT_IMAGE_OR_BOOT',source_root=str(ROOT),helper=file_ref(HELPER),rootfs_policy_projection=True,board='NOT_BOOTED',sd='NOT_WRITTEN',gpu_raw_cycles=None);rec['kernel_reuse']=kernel_reuse_audit();rec['presets']=presets_audit();fw=rt['bind_kernel'].firmware_inputs(ROOT,4,mosaic_variant='reuse-v8');need(len(fw['entries'])==16,'actual joint16 required');rec['firmware']=dict(variant='reuse-v8',catalog_sha256=digest(fw['catalog']),execution_sha256=digest(fw['execution']),entries=len(fw['entries']))
 for label,path in [('final_source_lock',SOURCE_LOCK),('final_userspace_cross',CROSS_RECEIPT),('final_models_host',MODEL_RECEIPT),('final_host_parser',HOST_PARSER)]:
  if not path.is_file():pending.append(label)
  else:rec[label]=file_ref(path,16<<20 if label!='final_host_parser' else 512<<20)
 if SOURCE_LOCK.is_file():
  observed=rt['cross_common'].verify_source_lock(ROOT,SOURCE_LOCK,source_lock_sha or file_ref(SOURCE_LOCK)['sha256']);rec['source_snapshot_id']=observed['source_snapshot_id'];rec['runtime_source_id']=observed['runtime_source_id']
 if CROSS_RECEIPT.is_file() and SOURCE_LOCK.is_file():
  if cross_sha:need(file_ref(CROSS_RECEIPT)['sha256']==cross_sha,'explicit final cross pin differs')
  cross=json_file(CROSS_RECEIPT);rt['bind_kernel'].require_cross_inputs(cross,observed,'aarch64-s8-joint');need(canonical(cross['source_before'])==canonical(observed)==canonical(cross['source_after']),'full final cross/source lock identity differs')
 if MODEL_RECEIPT.is_file():
  if models_sha:need(file_ref(MODEL_RECEIPT)['sha256']==models_sha,'explicit final model receipt pin differs')
  models_doc=json_file(MODEL_RECEIPT);installed=validate_model_rows(models_doc);rec['models']=package_audit(installed,fw);rec['installed_models']=installed
 rec['pending_gates']=pending;rec['status']='PREPARED_PENDING_FINAL_INPUTS' if pending else 'INPUT_CONTENT_PREFLIGHT_PASSED_NOT_PACKED';return rec


def failed_receipt(record,error):
 out=dict(record);out.update(natural_exit=1,status='FAILED_NOT_ACCEPTED',error=repr(error));return out

def actual_run(args,ev):
 need(all(is_digest(v) for v in [args.source_lock_sha,args.cross_sha,args.models_sha]),'explicit final source-lock/cross/model SHA pins required for --run');rec=preflight(args.source_lock_sha,args.cross_sha,args.models_sha);need(not rec['pending_gates'],'final green inputs absent: '+repr(rec['pending_gates']));rt=load_runtime();repack=rt['repack'];cross=json_file(CROSS_RECEIPT);installed=rec['installed_models'];before=rt['cross_common'].verify_source_lock(ROOT,SOURCE_LOCK,args.source_lock_sha)
 need(not WORK.exists() and not WORK.is_symlink() and not RELEASE.exists() and not RELEASE.is_symlink(),'new untouched image work/release paths required; preserve prior attempts');budget_before=task_logical_bytes();need(budget_before<BUDGET,'G task already exceeds50GB');WORK.mkdir();stage=WORK/'stage';stage.mkdir();files={};directories={};written=0
 build_id='VXU-S8-JOINT-NN-IMAGE-P8-R13-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');epoch=int(time.time());rec.update(scope='ACTUAL_R13_MATCHED_USERSPACE_NINE_MODEL_FIT_ROOTFS_CONTENT_NOT_BOOT_GPU_DP',natural_exit=1,build_id=build_id,source_snapshot_id=before['source_snapshot_id'],runtime_source_id=before['runtime_source_id'],service_startup='OFF_EXPLICIT_OPT_IN',initial_checks='FAST_INITIAL_LOAD_IDENTITY_RETAINED',runtime_checks='FAST_PER_JOB_CONTENT_CHECKS_OMITTED_V4_UNCHECKED',g_task_logical_bytes_before=budget_before)
 def save():(ev/'receipt.json').write_bytes(wire(rec))
 def durable(path,raw):
  nonlocal written
  need(budget_before+written+len(raw)<BUDGET,'new payload would exceed G50GB budget');path.parent.mkdir(parents=True,exist_ok=True)
  with path.open('xb') as f:need(f.write(raw)==len(raw),'short durable output');f.flush();os.fsync(f.fileno())
  need(regular(path,512<<20)==raw,'output readback differs');written+=len(raw)
 def put(name,raw,mode):
  repack.member_name(name);need(name not in files and type(mode) is int,'unique typed archive overlay required');durable(stage/name,raw);files[name]=dict(path=str(stage/name),bytes=len(raw),sha256=digest(raw),mode=mode)
 def tree(source,prefix):
  directories[prefix]=0o500
  for folder,dirs,names in os.walk(source,followlinks=False):
   for d in dirs:
    q=P(folder,d);need(not q.is_symlink(),'model directory symlink');directories[prefix+'/'+q.relative_to(source).as_posix()]=0o500
   for name in sorted(names):q=P(folder,name);need(stat.S_ISREG(q.lstat().st_mode),'regular tree member');put(prefix+'/'+q.relative_to(source).as_posix(),regular(q),0o400)
 try:
  save();base=repack.baseline();rec['baseline']={n:evidence(b) for n,b in base['loaded'].items()}
  for row in cross['installation']:
   need(row['destination'] in ['/usr/bin/vxgpud','/usr/bin/vxgpuctl'] and P(row['source']).is_absolute() and P(row['source']).resolve().is_relative_to(GROOT),'actual current cross daemon/client source required');raw=regular(row['source']);need(digest(raw)==row['sha256'],'cross output bytes differ');put(row['destination'][1:],raw,0o755)
  demo=regular(ROOT/'tools/demo.py').replace(b'\r\n',b'\n');need(demo.startswith(b'#!') and b'\r' not in demo.split(b'\n',1)[0],'native Python demo shebang');put('usr/bin/demo',demo,0o755)
  fw=rt['bind_kernel'].firmware_inputs(ROOT,4,mosaic_variant='reuse-v8');put('lib/firmware/vxl/catalog-v1.bin',fw['catalog'],0o444);put('lib/firmware/vxl/execution-v1.bin',fw['execution'],0o444);put('usr/share/vxgpu/catalog.bin',fw['catalog'],0o444);directories['lib/firmware/vxl/programs']=0o755
  for row in fw['entries']:put('lib/firmware/vxl/programs/'+row['entry_id']+'.bin',row['payload'],0o444)
  for name in EXPECTED_NAMES:tree(MODELS/name,'usr/share/vxgpu/models/'+name)
  directories['usr/share/vxgpu/models']=0o500;tree(PRESETS,PRESET_TARGET[1:]);installation=unique_json(base['entries']['etc/vxgpud/installation.json']['data']);installation['nn']=[copy.deepcopy(installed[n]) for n in EXPECTED_NAMES];installation['catalog']=dict(path='/usr/share/vxgpu/catalog.bin',sha256=digest(fw['catalog']));installation['p8']['startup_selftest_frames']=1;installation['mosaic_export']['build_id']=build_id
  config=unique_json(base['entries']['etc/vxgpud/config.json']['data']);config['runtime_integrity']='fast';raw_config=wire(config);put('etc/vxgpud/config.json',raw_config,0o600);put('etc/default/vxgpud',b'UNIFIED_SERVICE_ENABLED=0\n',0o644);raw_installation=wire(installation);put('etc/vxgpud/installation.json',raw_installation,0o400);put('etc/vxgpud/installation.sha256',(digest(raw_installation)+'\n').encode(),0o400);put('etc/codex-build-id',(build_id+'\n').encode(),0o644)
  put('etc/init.d/codex-direct-network',network_payload(regular(ROOT/'petalinux/unified/network-init')),0o755);init=regular(ROOT/'petalinux/unified/service-init').replace(b'\r\n',b'\n');need(b'S8_BOOT_IDENTITY' in init and b'command -v dropbearkey' in init and b'dropbearmulti' in init and b'S8_R6_BOOT_IDENTITY' not in init,'actual current PATH/public-only boot identity init required');put('etc/init.d/vxgpud',init,0o755)
  pub=regular(PUBLIC_KEY,4096).strip();need(pub.startswith(b'ssh-rsa ') and b'\n' not in pub,'only already-authorized A public key read');passwd=base['entries']['etc/passwd']['data'].decode();home=next(line.split(':')[5] for line in passwd.splitlines() if line.startswith('root:'));need(home in ['/root','/home/root'],'original root home required');prefix=home[1:]+'/.ssh';directories[prefix]=0o700;old=base['entries'].get(prefix+'/authorized_keys',{}).get('data',b'');keys=old if pub in old.splitlines() else old.rstrip()+b'\n'+pub+b'\n';put(prefix+'/authorized_keys',keys.lstrip(b'\n'),0o600)
  for tag,name in [('network','etc/init.d/codex-direct-network'),('init','etc/init.d/vxgpud')]:
   r=subprocess.run(['/bin/sh','-n',str(stage/name)],stdout=subprocess.PIPE,stderr=subprocess.PIPE);(ev/(tag+'-syntax.stdout')).write_bytes(r.stdout);(ev/(tag+'-syntax.stderr')).write_bytes(r.stderr);need(r.returncode==0,'actual shell syntax failed: '+tag)
  rec['host_config']=host_config_check(raw_config,ev);manifest=copy.deepcopy(unique_json(base['entries']['usr/share/vxgpu/payload-manifest.json']['data']));manifest['build_id']=build_id;prefixes=['usr/share/vxgpu/models','usr/share/vxgpu/test-models','usr/share/vxgpu/hunet-fixed-inputs','lib/firmware/vxl/programs'];manifest['files']={n:r for n,r in manifest['files'].items() if not any(n==q or n.startswith(q+'/') for q in prefixes)}
  for name,row in files.items():manifest['files'][name]={k:row[k] for k in ['bytes','sha256','mode']}
  put('usr/share/vxgpu/payload-manifest.json',wire(manifest),0o444);present_prefixes,absent_prefixes=existing_removal_prefixes(repack.cpio_entries(base['cpio']),prefixes);rec['prefix_inventory']=dict(existing=present_prefixes,absent=absent_prefixes,absent_descendants_proved_empty=True);removals=repack.expand_prefix_removals(base['cpio'],present_prefixes,keep=set(files)|set(directories));raw_files={n:(regular(row['path']),row['mode']) for n,row in files.items()};cpio=repack.overlay_newc(base['cpio'],raw_files,directories,removals,epoch);root_audit=audit_rootfs_r10(cpio,build_id,cross_receipt=cross,installed_models=installed);rec['host_installation']=host_installation_check(repack.cpio_entries(cpio)['etc/vxgpud/installation.json']['data'],ev);need(not root_audit['pending_gates'],'all actual rootfs gates required')
  backend=base['dt']['backend'];updates={(backend,'vortex,catalog-sha256'):hashlib.sha256(fw['catalog']).digest(),(backend,'vortex,execution-sha256'):hashlib.sha256(fw['execution']).digest()};dt=repack.replace_fdt(base['fit']['dtb'],updates);device_tree=rt['audit_kernel'].audit_device_tree(dt,base['audit']['provider'],digest(fw['catalog']),digest(fw['execution']));kernel=regular(KERNEL/'Image');need(digest(kernel)==KERNEL_IMAGE_SHA,'frozen actual kernel changed');unused,nodes,paths,hashes=repack.selected_fit(base['loaded']['image.ub']);gz=repack.gzip_bytes(cpio);kgz=repack.gzip_bytes(kernel);changes={('/','description'):build_id.encode()+b'\0',('/','timestamp'):struct.pack('>I',epoch)}
  for role,data in [('kernel',kgz),('fdt',dt),('ramdisk',gz)]:changes[(paths[role],'data')]=data;changes[(hashes[role],'value')]=hashlib.sha256(data).digest()
  packed=repack.replace_fdt(base['loaded']['image.ub'],changes);need(len(packed)<512<<20 and rt['audit_image'].fit_payloads(packed)==dict(kernel=kernel,dtb=dt,rootfs_gzip=gz),'new FIT actual selected payload equality');durable(WORK/'image.ub',packed);durable(WORK/'system.dtb',dt);durable(WORK/'rootfs.cpio.gz',gz);check=rt['audit_image'].fit_payloads(regular(WORK/'image.ub'));need(check['kernel']==kernel and check['dtb']==dt and repack.cpio_entries(repack.expand_gzip(check['rootfs_gzip'],repack.MAX_CPIO))==repack.cpio_entries(cpio),'readback FIT/rootfs bytes differ');need(rt['cross_common'].snapshot(ROOT)==before,'final locked production source changed');need(file_ref(HELPER)['sha256']==HELPER_SHA,'frozen helper changed during run')
  RELEASE.mkdir(parents=True,exist_ok=False)
  for name,raw in [('image.ub',packed),('BOOT.BIN',base['loaded']['BOOT.BIN']),('boot.scr',base['loaded']['boot.scr']),('kernel.config',regular(KERNEL/'kernel.config')),('source-lock-r13-668.json',regular(SOURCE_LOCK,16<<20)),('userspace-cross-receipt.json',regular(CROSS_RECEIPT,16<<20)),('models-host-receipt.json',regular(MODEL_RECEIPT,16<<20))]:durable(RELEASE/name,raw)
  delivered=rt['audit_image'].fit_payloads(regular(RELEASE/'image.ub'));need(delivered==check,'delivered image readback differs verified work image');need(regular(RELEASE/'BOOT.BIN')==base['loaded']['BOOT.BIN'] and regular(RELEASE/'boot.scr')==base['loaded']['boot.scr'],'original BOOT/script changed')
  rec.update(natural_exit=0,status='ACTUAL_CONTENT_AUDITED_NOT_BOOTED_NOT_SD_NOT_GPU_DP',rootfs_audit=root_audit,device_tree=device_tree,fit_payloads={k:evidence(v) for k,v in delivered.items()},rootfs_uncompressed=evidence(cpio),overlay_files=len(files),removed_stale_files=len(removals),management=dict(primary_ip='192.168.13.10/24',secondary_ip='10.0.0.10/24',public_key_sha256=digest(pub),public_key_only=True),p8_selftests=dict(startup=1,maintenance_policy=100,executed=False),model_defaults='HUNET_GPU_DEFAULT_NODE9_AND_ALL16_OPT_IN_MOSAIC_C256_P4096_OPTIONS0_PROFILE_AND_SMOKE_EXPLICIT',artifacts={q.name:file_ref(q,16<<20 if q.suffix=='.json' else 512<<20) for q in RELEASE.iterdir() if q.is_file()},source_lock=file_ref(SOURCE_LOCK,16<<20),cross_receipt=file_ref(CROSS_RECEIPT,16<<20),models_receipt=file_ref(MODEL_RECEIPT,16<<20));rec['g_task_logical_bytes_after_payload']=task_logical_bytes();need(rec['g_task_logical_bytes_after_payload']<BUDGET,'final G task50GB cap');final_raw=wire(rec);checksums=''.join(file_ref(q,512<<20)['sha256']+'  '+q.name+'\n' for q in sorted(RELEASE.iterdir()) if q.is_file())+digest(final_raw)+'  image-content-audit.json\n';durable(RELEASE/'SHA256SUMS',checksums.encode());durable(RELEASE/'image-content-audit.json',final_raw);save();return rec
 except BaseException as error:rec=failed_receipt(rec,error);save();raise

def main():
 resource.setrlimit(resource.RLIMIT_CORE,(0,0));os.environ['TMPDIR']=str(TASK/'tmp');parser=argparse.ArgumentParser(description=__doc__);mode=parser.add_mutually_exclusive_group(required=True);mode.add_argument('--prepare',action='store_true');mode.add_argument('--run',action='store_true');parser.add_argument('--label',required=True);parser.add_argument('--source-lock-sha');parser.add_argument('--cross-sha');parser.add_argument('--models-sha');args=parser.parse_args();ev=evidence_path(args.label);ev.mkdir(parents=True,exist_ok=False);(TASK/'tmp').mkdir(exist_ok=True);(ev/'packer-source-used.py').write_bytes(regular(P(__file__),1<<20))
 try:
  rec=preflight(args.source_lock_sha,args.cross_sha,args.models_sha) if args.prepare else actual_run(args,ev)
  if args.prepare:rec['natural_exit']=0
  (ev/'receipt.json').write_bytes(wire(rec));print(json.dumps({k:rec[k] for k in ['scope','status','pending_gates'] if k in rec},indent=2));return 0
 except BaseException as error:
  if not (ev/'receipt.json').exists():(ev/'receipt.json').write_bytes(wire(dict(scope='OFFLINE_PREPARATION_OR_PACK_FAILURE_NOT_ACCEPTED',natural_exit=1,error=repr(error),board='NOT_BOOTED',sd='NOT_WRITTEN',gpu_raw_cycles=None)))
  raise
if __name__=='__main__':raise SystemExit(main())
