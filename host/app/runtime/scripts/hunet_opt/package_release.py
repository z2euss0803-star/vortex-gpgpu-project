"""Closed program10/v4 release; no deployment, device or media operations."""
import argparse,gzip,hashlib,importlib.util,io,json,os,pathlib,re,shutil,struct,subprocess,sys,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT));sys.path.insert(0,str(ROOT/'scripts/hunet'))
from scripts.hunet_opt.build_identity import canonical,verify_snapshot
from scripts.hunet_opt.catalog import compile_catalog,validate_catalog,canonical_bytes
from scripts.hunet_opt.cross import elf_machine
PREFIXES={'core','driver','include','runtime','userspace','mock','tools','generated','scripts','tests','contracts','vendor','docs','device','delivery'}
TOP={'Makefile','README.md','.gitignore','.gitattributes'}
def sha(raw):return hashlib.sha256(raw).hexdigest()
def need(ok,reason):
 if not ok:raise ValueError(reason)
def write(path,value):pathlib.Path(path).write_bytes(canonical(value))

def write_runtime_assets(root,runtime):
 value=runtime.get('libm_sha256','');need(re.fullmatch('[a-f0-9]{64}',value) is not None,'LIBM_SHA_IDENTITY')
 (pathlib.Path(root)/'cpu/libm.sha256').write_bytes((value+'\n').encode('ascii'))

def check_runtime_assets(root,runtime):
 value=runtime.get('libm_sha256','');need(re.fullmatch('[a-f0-9]{64}',value) is not None,'LIBM_SHA_IDENTITY')
 need(regular(root,'cpu/libm.sha256')==(value+'\n').encode('ascii'),'MANDATORY_CLI_LIBM_ASSET');return 'PASS'
def hardware_from_verified(verified,manifest,profile):
 need(verified.get('integrity')=='PASS','BASE_RELEASE_NOT_VERIFIED');result={}
 for k in ('bit_sha256','xsa_sha256'):
  value=manifest.get(k,'');need(re.fullmatch('[0-9a-f]{64}',value) and value==profile.get(k),'FROZEN_W4_PROFILE:'+k);result[k]=value
 return result
def numeric_review(candidate):
 return dict(scope='HOST_REFERENCE_AND_UNTESTED_TARGET_REVIEW',board='NOT_RUN',candidate_full_host_graph='R2_PASS_ORT_FAIL8105_HISTORY_DIFFERENT' if candidate in ('C1_T','G1') else 'NOT_RUN',shared_transform_tests='PASS_SOFTWARE_ONLY',formal_approval=False,hardware_performance='NOT_RUN',combinable=candidate!='G2_TIMING')
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
legacy=load('opt_base_release_v1',ROOT/'scripts/hunet/package_release.py')
hybrid=load('opt_cpu_roles_v2',ROOT/'scripts/hunet_hybrid/package_release.py')
def safe(name):
 need(isinstance(name,str) and name and not name.startswith('/') and '\\' not in name and ':' not in name and all(p not in ('','..','.') for p in name.split('/')),'RELATIVE_REGULAR_PATH');return name
def regular(root,name):
 p=pathlib.Path(root)
 for part in safe(name).split('/'):
  p=p/part;need(not p.is_symlink(),'SYMLINK:'+name)
 return p.read_bytes()
def source_inventory(root):
 root=pathlib.Path(root).resolve()
 if not (root/'.git').exists():
  path=root.with_name(root.name+'.inventory.json');need(path.is_file(),'SEALED_SOURCE_INVENTORY_REQUIRED');i=json.loads(path.read_bytes());verify_snapshot(root,i)
  for p in root.rglob('*'):
   n=p.relative_to(root).as_posix()
   if n.split('/')[0] in ('build','evidence','.superpowers') or '__pycache__' in p.parts:continue
   if p.is_file() or p.is_symlink():need(n in i['files'] and not p.is_symlink(),'UNTRACKED_SNAPSHOT_SOURCE:'+n)
  return i
 def git(*args):return subprocess.check_output(['git','-c','core.quotepath=false','-C',str(root),*args])
 need(not git('diff','--name-only','HEAD').strip(),'DIRTY_TRACKED_SOURCE')
 for n in git('ls-files','--others','--exclude-standard').decode().splitlines():need(n.startswith(('evidence/','.superpowers/','release/','build/')),'UNTRACKED_SOURCE:'+n)
 commit=git('rev-parse','HEAD').decode().strip();files={}
 with tarfile.open(fileobj=io.BytesIO(git('archive',commit)),mode='r:') as tf:blobs={m.name:tf.extractfile(m).read() for m in tf if m.isfile()}
 for row in git('ls-tree','-r',commit).decode().splitlines():
  info,name=row.split('\t',1);mode,kind,blob=info.split()
  if name not in TOP and name.split('/')[0] not in PREFIXES:continue
  need(kind=='blob' and mode in ('100644','100755'),'SOURCE_NONREGULAR');raw=blobs[safe(name)];need(hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==blob,'GIT_BLOB')
  files[name]=dict(bytes=len(raw),sha256=sha(raw),git_blob=blob)
 need(bool(files),'EMPTY_SOURCE');return dict(schema=1,source_commit=commit,files=files)
def validate_payload(root,records,manifest=None):
 root=pathlib.Path(root)
 for n,v in records.items():
  b=regular(root,n);need(len(b)==v['bytes'] and sha(b)==v['sha256'],'PAYLOAD_IDENTITY:'+n)
 actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() or p.is_symlink()}
 need(actual==set(records)|({manifest} if manifest else set()),'UNCLOSED_RELEASE');return 'PASS'
def make_binding(profile,wire,source,weights,jobs,aux,model,cpu,contract,stages,optimization,descriptors,layout):
 # 7 v1 hashes, 3 v2 hashes, reserved32, 3 v4 hashes, reserved32.
 hashes=(profile,wire,source,weights,jobs,aux,model,cpu,contract,stages,optimization,descriptors,layout)
 need(all(isinstance(x,bytes) and len(x)==32 for x in hashes),'BINDING_HASH_WIDTH')
 return struct.pack('<8I',0x44424c48,4,512,4,10,31,0,0)+b''.join(hashes[:10])+bytes(32)+b''.join(hashes[10:])+bytes(32)
def release_header(binding,program):
 prefix=struct.pack('<2I',program['wire_bytes'],program['immutable_spans'][0]['bytes'])+bytes.fromhex(program['wire_sha256'])+bytes.fromhex(program['immutable_spans'][0]['sha256'])+binding+hashlib.sha256(binding).digest();need(len(prefix)==616,'RELEASE_PREFIX_SIZE')
 # Source is frozen before binaries. Final CPU hash lives only in this later
 # kernel header/external binding; it was never an input to its own compilation.
 return ('#ifndef VXL_OPT_RELEASE_H\n#define VXL_OPT_RELEASE_H\nstatic const union {struct vxl_opt_release value;vxl_u8 raw[sizeof(struct vxl_opt_release)];} vxl_opt_release_union __attribute__((used))={.raw={'+','.join('0x%02x'%x for x in prefix)+'}};\nstatic inline struct vxl_opt_release vxl_opt_release_get(void){struct vxl_opt_release r=vxl_opt_release_union.value;r.catalog=vxl_opt_catalog_default();r.digest_backend=(r.catalog->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE;return r;}\n#define vxl_opt_release_bound (vxl_opt_release_get())\n#endif\n').encode()
def validate_cross(receipt,kind,identity,candidate):
 need(receipt.get('exit')==0 and receipt.get('audit',{}).get('integrity')=='PASS' and not receipt.get('error'),'ACTUAL_GREEN_CROSS_RECEIPT_REQUIRED')
 need(all(receipt.get(k)==identity[k] for k in ('source_commit','source_digest')),'CROSS_SOURCE_COMMIT_MISMATCH')
 need(receipt.get('candidate')==candidate,'CROSS_CANDIDATE_MISMATCH')
 if kind=='cpu':need(receipt.get('scope')=='OPT_CPU_AARCH64_BUILD' and receipt.get('compiler',{}).get('target')=='aarch64-xilinx-linux' and re.search(r'\b9\.2\.0\b',receipt.get('compiler',{}).get('version','')) and receipt.get('sha2_isolation',{}).get('integrity')=='PASS','ACTUAL_AARCH64_SDK_REQUIRED')
 elif kind=='rv32':need(receipt.get('scope')=='RV32_COMPILE_AND_STATIC_AUDIT_NOT_BOARD','ACTUAL_RV32_REQUIRED')
 else:raise ValueError('CROSS_KIND')
 return 'PASS'
def source_archive(source,inventory,path):
 # Native immutable snapshots contain canonical Git bytes. Windows callers can
 # export from Git, with clean-filter checking performed by source_inventory.
 with pathlib.Path(path).open('xb') as fp:
  with gzip.GzipFile(fileobj=fp,mode='wb',mtime=0,filename='') as gz:
   with tarfile.open(fileobj=gz,mode='w') as tf:
    for n,v in sorted(inventory['files'].items()):
     raw=regular(source,n);need(sha(raw)==v['sha256'],'SNAPSHOT_CANONICAL_BYTES_REQUIRED:'+n);t=tarfile.TarInfo(n);t.size=len(raw);t.mode=0o644;t.mtime=0;tf.addfile(t,io.BytesIO(raw))
def _file_inventory(root):return {p.relative_to(root).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes())) for p in sorted(root.rglob('*')) if p.is_file()}
def source_header(identity):
 return ('#ifndef VXL_HUNET_SOURCE_H\n#define VXL_HUNET_SOURCE_H\nstatic const volatile vxl_u8 vxl_hunet_source_sha[32] __attribute__((used))={'+','.join('0x%02x'%x for x in bytes.fromhex(identity['source_digest']))+'};\nstatic const char vxl_hunet_source_commit[] __attribute__((used))="'+identity['source_commit']+'";\n#endif\n').encode()
def hybrid_header(base_binding,binding,identity):
 hb=bytearray(base_binding);hb[96:128]=bytes.fromhex(identity['source_digest']);hb=struct.pack('<8I',0x44424c48,2,384,4,8,7,0,0)+bytes(hb[32:])+binding[256:352]+bytes(32);need(len(hb)==384,'HYBRID_HEADER_SIZE')
 return ('#ifndef VXL_HYBRID_RELEASE_H\n#define VXL_HYBRID_RELEASE_H\nstatic const vxl_u8 vxl_hybrid_binding_bytes[384] __attribute__((used))={'+','.join('0x%02x'%x for x in hb)+'};\n#endif\n').encode()
def binding_for(root,identity,program):
 root=pathlib.Path(root);old=(root/'base/binding.bin').read_bytes();need(len(old)==256,'BASE_BINDING')
 return make_binding(old[32:64],bytes.fromhex(program['wire_sha256']),bytes.fromhex(identity['source_digest']),old[128:160],hashlib.sha256((root/'jobs.bin').read_bytes()).digest(),old[192:224],old[224:256],hashlib.sha256((root/'cpu/gelu.elf').read_bytes()).digest(),hashlib.sha256((root/'cpu-contract.json').read_bytes()).digest(),hashlib.sha256((root/'cpu-stages.json').read_bytes()).digest(),hashlib.sha256((root/'optimization-contract.json').read_bytes()).digest(),hashlib.sha256((root/'descriptors.bin').read_bytes()).digest(),hashlib.sha256((root/'layout.json').read_bytes()).digest())
def package_release(source,rv32,cpu,out,baseline=None,software=None):
 source,rv32,cpu,out=map(lambda p:pathlib.Path(p).resolve(),(source,rv32,cpu,out));inventory=source_inventory(source);identity=verify_snapshot(source,inventory)
 need(not out.exists(),'FRESH_RELEASE_REQUIRED');gpu=json.loads((rv32/'receipt.json').read_bytes());cpu_receipt=json.loads((cpu/'receipt.json').read_bytes());candidate=gpu['candidate']
 validate_cross(gpu,'rv32',identity,candidate);validate_cross(cpu_receipt,'cpu',identity,candidate)
 need(software and software.get('natural_exit')==0 and software.get('source_digest')==identity['source_digest'] and software.get('scope')=='UBUNTU_SOFTWARE_NOT_GPU_NOT_BOARD','MATCHING_SOFTWARE_VALIDATION_REQUIRED')
 baseline=pathlib.Path(baseline) if baseline else pathlib.Path(gpu.get('baseline_release',''))
 checked=legacy.verify_release(baseline/'base');need(checked['integrity']=='PASS','FROZEN_BASE_RELEASE:'+str(checked));hardware=hardware_from_verified(checked,json.loads((baseline/'base/release.json').read_bytes()),json.loads((source/'contracts/profiles/hw4-hunet-v5.json').read_bytes()))
 catalog=json.loads((source/'generated/hunet_opt'/candidate/'catalog.json').read_bytes());validate_catalog(catalog);need(gpu['catalog_sha256']==sha(canonical_bytes(catalog)),'GPU_CATALOG_IDENTITY')
 out.mkdir(parents=True);shutil.copytree(baseline/'base',out/'base');write(out/'SOURCE_SHA256.json',inventory);source_archive(source,inventory,out/'source.tar.gz')
 program_dir=out/'program10';program_dir.mkdir()
 for n in ('kernel.elf','kernel.vxbin','code.bin','kernel.map','kernel.disasm','kernel.headers','program.json','receipt.json'):
  if (rv32/n).is_file():shutil.copyfile(rv32/n,program_dir/n)
 program=json.loads((program_dir/'program.json').read_bytes());need(sha((program_dir/'code.bin').read_bytes())==program['wire_sha256'],'WIRE_CHANGED')
 from audit_program import audit_program
 audit_program(program_dir/'kernel.elf',program_dir/'code.bin',program)
 if candidate in ('C1_T','C1_H'):need(program['wire_sha256']=='8bb9f37b072531702377f66c04aa25b9b04d6543267a91225f7807534bc25b5c','FROZEN_WIRE_BYTES')
 (out/'cpu').mkdir()
 for src,dest in ((cpu/'objects/vxl_hunet_opt',out/'cpu/gelu.elf'),(cpu/'objects/libvortex-opt.so',out/'cpu/libvortex-opt.so')):elf_machine(src.read_bytes(),2,183);shutil.copyfile(src,dest)
 for n in ('build.log','build.log.exit','cli.map'):shutil.copyfile(cpu/n,out/'cpu'/n)
 write(out/'cpu/build.json',cpu_receipt);write(out/'cpu/runtime.json',cpu_receipt['cpu_runtime'])
 write_runtime_assets(out,cpu_receipt['cpu_runtime'])
 need((cpu/'build.log.exit').read_text().strip()=='0','NATURAL_CPU_EXIT');need(sha((out/'cpu/gelu.elf').read_bytes())==cpu_receipt['elfs']['vxl_hunet_opt']['sha256'] and sha((out/'cpu/libvortex-opt.so').read_bytes())==cpu_receipt['elfs']['libvortex-opt.so']['sha256'],'CPU_CROSS_BINARY')
 write(out/'cpu-contract.json',hybrid.cpu_contract());write(out/'cpu-stages.json',hybrid.freeze.freeze_contract(source))
 for dest,name in [('catalog.json','catalog.json'),('layout.json','layout.json'),('jobs.bin','jobs.bin'),('graph-descriptors.bin','graph-descriptors.bin'),('matrix-descriptors.bin','matrix-descriptors.bin')]:shutil.copyfile(source/'generated/hunet_opt'/candidate/name,out/dest)
 (out/'descriptors.bin').write_bytes((out/'matrix-descriptors.bin').read_bytes()+(out/'graph-descriptors.bin').read_bytes())
 write(out/'optimization-contract.json',dict(contract=json.loads((source/'contracts/hunet_opt_v2.json').read_bytes()),candidate=candidate,policy=catalog['layout']['execution_policy'],logical_sha256=catalog['logical_sha256'],aux_sha256=catalog['aux_sha256'],cpu_runtime=cpu_receipt['cpu_runtime'],cpu_library_sha256=sha((out/'cpu/libvortex-opt.so').read_bytes())))
 (out/'references').mkdir();numeric=load('opt_package_numeric_reader',source/'scripts/hunet/audit_run.py');references={}
 for key,name in [('r2','R2.npy'),('ort','ORT.npy')]:
  raw=struct.pack('<8105f',*numeric.read_npy_row(out/'base/references'/name,0));p=out/'references'/(key+'.f32');p.write_bytes(raw);references[key]=dict(path=p.relative_to(out).as_posix(),bytes=len(raw),sha256=sha(raw))
 model=json.loads((source/'contracts/hunet_v5.json').read_bytes());record=next(f for f in model['files'] if f['path'].endswith('/GF_class_indices.json'));raw=(source.parent.parent/record['path']).read_bytes();need(len(raw)==record['bytes'] and sha(raw)==record['sha256'],'LABEL_IDENTITY');(out/'references/labels.json').write_bytes(raw);references['labels']=dict(path='references/labels.json',bytes=len(raw),sha256=sha(raw))
 raw=(source/'tests/hunet_opt/fixtures/historical-39fb432d-logits.f32').read_bytes();need(sha(raw)=='58a75990aad6711810065fa7a3c83e81a3eaaba13769f0395eac95dd7fa48654','HISTORY_IDENTITY');(out/'references/history.f32').write_bytes(raw);references['history']=dict(path='references/history.f32',bytes=len(raw),sha256=sha(raw))
 b=binding_for(out,identity,program);(out/'binding-opt.bin').write_bytes(b);(out/'build').mkdir();(out/'build/vxl_opt_release.h').write_bytes(release_header(b,program))
 (out/'build/vxl_hunet_source.h').write_bytes(source_header(identity));(out/'build/vxl_hybrid_release.h').write_bytes(hybrid_header((out/'base/binding.bin').read_bytes(),b,identity))
 write(out/'software-validation.json',software)
 result=dict(schema=1,scope='HUNET_OPT_OFFLINE_RELEASE',profile=4,program_id=10,candidate=candidate,**identity,binding_sha256=sha(b),**hardware,references=references,policy=catalog['layout']['execution_policy'],numeric_review=numeric_review(candidate),deployable=False,formal_numeric_authority=False,gpu_cycles=None,statuses=dict(UBUNTU='PASS',RV32='PASS_STATIC_OR_FROZEN_ALIAS',CPU='PASS_CROSS',KERNEL_BOUND='NOT_RUN',IMAGE='NOT_RUN',H0='NOT_RUN',H1='NOT_RUN'),files=_file_inventory(out))
 write(out/'release-opt.json',result);verified=verify_release(out);need(verified['integrity']=='PASS',str(verified));return result
def verify_release(release):
 result=dict(integrity='FAIL',scope='HUNET_OPT_RELEASE_AUDIT',deployable=False)
 try:
  root=pathlib.Path(release);m=json.loads(regular(root,'release-opt.json'));need(m.get('schema')==1 and m.get('scope')=='HUNET_OPT_OFFLINE_RELEASE' and m.get('profile')==4 and m.get('program_id')==10 and m.get('deployable') is False and m.get('gpu_cycles') is None,'OPT_RELEASE_SCHEMA');validate_payload(root,m['files'],'release-opt.json')
  inventory=json.loads(regular(root,'SOURCE_SHA256.json'));identity=dict(source_commit=inventory['source_commit'],source_digest=sha(canonical(inventory)));need(all(identity[k]==m[k] for k in identity),'SOURCE_BINDING');seen=set();source={}
  with tarfile.open(root/'source.tar.gz') as tf:
   for t in tf:
    safe(t.name);need(t.isfile() and t.name in inventory['files'] and t.name not in seen,'SOURCE_ARCHIVE_CLOSED');seen.add(t.name);raw=tf.extractfile(t).read();v=inventory['files'][t.name];need(len(raw)==v['bytes'] and sha(raw)==v['sha256'] and hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==v['git_blob'],'SOURCE_ARCHIVE_HASH');source[t.name]=raw
  need(seen==set(inventory['files']),'MISSING_SOURCE_MEMBER');candidate=m['candidate'];catalog=json.loads(regular(root,'catalog.json'));validate_catalog(catalog);need(catalog['candidate']==candidate and m['policy']==catalog['layout']['execution_policy'],'CANDIDATE_IDENTITY')
  for dest,name in [('catalog.json','catalog.json'),('layout.json','layout.json'),('jobs.bin','jobs.bin'),('graph-descriptors.bin','graph-descriptors.bin'),('matrix-descriptors.bin','matrix-descriptors.bin')]:need(regular(root,dest)==source['generated/hunet_opt/'+candidate+'/'+name],'SOURCE_BOUND_GENERATED:'+dest)
  need(regular(root,'descriptors.bin')==regular(root,'matrix-descriptors.bin')+regular(root,'graph-descriptors.bin'),'DESCRIPTOR_IDENTITY');base=legacy.verify_release(root/'base');need(base['integrity']=='PASS','FROZEN_BASE:'+str(base))
  hardware=hardware_from_verified(base,json.loads(regular(root,'base/release.json')),json.loads(source['contracts/profiles/hw4-hunet-v5.json']))
  for k in ('bit_sha256','xsa_sha256'):need(m[k]==hardware[k],'FROZEN_HARDWARE:'+k)
  need(m.get('numeric_review')==numeric_review(candidate) and m.get('formal_numeric_authority') is False,'NUMERIC_REVIEW_NOT_APPROVED')
  cpu=json.loads(regular(root,'cpu/build.json'));gpu=json.loads(regular(root,'program10/receipt.json'));validate_cross(cpu,'cpu',identity,candidate);validate_cross(gpu,'rv32',identity,candidate)
  for n,role in [('cpu/gelu.elf','vxl_hunet_opt'),('cpu/libvortex-opt.so','libvortex-opt.so')]:raw=regular(root,n);elf_machine(raw,2,183);need(sha(raw)==cpu['elfs'][role]['sha256'],'CPU_ELF_IDENTITY')
  need(regular(root,'cpu/build.log.exit').strip()==b'0','NATURAL_CPU_EXIT');need(json.loads(regular(root,'cpu/runtime.json'))==cpu['cpu_runtime'],'LIBM_RUNTIME_IDENTITY');need(json.loads(regular(root,'cpu-contract.json'))==hybrid.cpu_contract(),'CPU_CONTRACT');hybrid.freeze.verify_baseline(json.loads(regular(root,'cpu-stages.json')),json.loads(source['contracts/hunet_v5.json']))
  check_runtime_assets(root,cpu['cpu_runtime'])
  program=json.loads(regular(root,'program10/program.json'));from audit_program import audit_program
  audit_program(root/'program10/kernel.elf',root/'program10/code.bin',program);need(sha(regular(root,'program10/code.bin'))==program['wire_sha256'],'GPU_WIRE_IDENTITY');need(gpu['program']['wire_sha256']==program['wire_sha256'],'GPU_BUILD_IDENTITY')
  if candidate in ('C1_T','C1_H'):need(program['wire_sha256']=='8bb9f37b072531702377f66c04aa25b9b04d6543267a91225f7807534bc25b5c','OLD_WIRE_ALIAS')
  else:need(program['program_id']==10 and gpu.get('stack_envelope',{}).get('admission')=='PASS' and gpu.get('actual_instructions',{}).get('wmma',0)>0,'ACTUAL_GPU_STATIC_AUDIT')
  b=binding_for(root,identity,program);need(regular(root,'binding-opt.bin')==b and sha(b)==m['binding_sha256'],'BINDING_V4');need(regular(root,'build/vxl_opt_release.h')==release_header(b,program),'RELEASE_HEADER_IDENTITY');need(regular(root,'build/vxl_hunet_source.h')==source_header(identity) and regular(root,'build/vxl_hybrid_release.h')==hybrid_header(regular(root,'base/binding.bin'),b,identity),'ATOMIC_SOURCE_HYBRID_HEADERS')
  contract=json.loads(regular(root,'optimization-contract.json'));need(contract==dict(contract=json.loads(source['contracts/hunet_opt_v2.json']),candidate=candidate,policy=catalog['layout']['execution_policy'],logical_sha256=catalog['logical_sha256'],aux_sha256=catalog['aux_sha256'],cpu_runtime=cpu['cpu_runtime'],cpu_library_sha256=sha(regular(root,'cpu/libvortex-opt.so'))),'OPTIMIZATION_CONTRACT')
  numeric=load('opt_verify_numpy_reader',ROOT/'scripts/hunet/audit_run.py')
  for k,name in [('r2','R2.npy'),('ort','ORT.npy')]:need(regular(root,m['references'][k]['path'])==struct.pack('<8105f',*numeric.read_npy_row(root/'base/references'/name,0)),'REFERENCE_ROW_DERIVATION:'+k)
  need(sha(regular(root,m['references']['history']['path']))=='58a75990aad6711810065fa7a3c83e81a3eaaba13769f0395eac95dd7fa48654' and sha(regular(root,m['references']['labels']['path']))=='ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f','HISTORY_LABELS')
  software=json.loads(regular(root,'software-validation.json'));need(software.get('natural_exit')==0 and software.get('source_digest')==identity['source_digest'] and software.get('scope')=='UBUNTU_SOFTWARE_NOT_GPU_NOT_BOARD','SOFTWARE_GATE')
  result.update(integrity='PASS',program_id=10,candidate=candidate,**identity,binding_sha256=sha(b),bit_sha256=m['bit_sha256'],xsa_sha256=m['xsa_sha256'],references=m['references'],gpu_cycles=None,board='NOT_RUN',h1='NOT_RUN')
 except Exception as e:result['reason']=str(e)
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=('package','verify'));p.add_argument('--source',type=pathlib.Path);p.add_argument('--rv32',type=pathlib.Path);p.add_argument('--cpu',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--baseline',type=pathlib.Path);p.add_argument('--software',type=pathlib.Path);a=p.parse_args();r=verify_release(a.out) if a.kind=='verify' else package_release(a.source,a.rv32,a.cpu,a.out,a.baseline,json.loads(a.software.read_bytes()));print(json.dumps({k:r.get(k) for k in ('integrity','scope','candidate','source_commit','source_digest','binding_sha256','reason')}));raise SystemExit(0 if r.get('integrity','PASS')=='PASS' else 1)
