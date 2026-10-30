"""Provider-isolated image payload and complete PetaLinux build; no media APIs."""
import pathlib,json,hashlib,shutil,stat,subprocess,tarfile,datetime,shlex,re,time
from scripts.unified.cross_common import sha,snapshot,provider,aarch64_target,new_build_output,verify_source_lock
from scripts.unified.bind_kernel import firmware_inputs,require_cross_inputs
from scripts.unified.audit_image import payload_errors

INTERFACES=b'auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet static\n    address 192.168.13.10\n    netmask 255.255.255.0\n'
# Image packaging choice within the unchanged trusted [1,4096] batch contract.
# This value is metadata, not a claim that the GPU watchdog/performance passed.
MOSAIC_IMAGE_BATCH=64
BOOT_TEXT=b'''setenv bootargs 'earlycon console=ttyPS0,115200 root=/dev/ram0 rw'
setenv fdt_high 0x0fffffff
setenv initrd_high 0x3fffffff
if fatload mmc 1:1 0x20000000 image.ub; then
    bootm 0x20000000
fi
exit 1
'''

def wire(value):return (json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True)+'\n').encode()

def bootgen_tool(settings,expected):
 """Bind the actual 2020.1 buildtools binary, never ambient host PATH."""
 settings=pathlib.Path(settings).resolve();p=settings.parent/'components/yocto/buildtools/sysroots/x86_64-petalinux-linux/usr/bin/bootgen'
 if not settings.is_file() or type(expected) is not str or not re.fullmatch('[0-9a-f]{64}',expected) or p.is_symlink() or not p.is_file():raise ValueError('explicit pinned PetaLinux2020.1 bootgen required')
 raw=p.read_bytes()
 if len(raw)<64 or raw[:7]!=b'\x7fELF\x02\x01\x01' or int.from_bytes(raw[18:20],'little')!=62 or hashlib.sha256(raw).hexdigest()!=expected:raise ValueError('actual matching host bootgen bytes/architecture required')
 return p

def boot_bif(im,bit):
 """Independent exact six-input BIF; DT load matches actual device-tree.bif."""
 im=pathlib.PurePosixPath(im);bit=str(bit);paths=[str(im/n) for n in ('zynqmp_fsbl.elf','pmufw.elf')]+[bit]+[str(im/n) for n in ('bl31.elf','system.dtb','u-boot.elf')]
 for p in paths:
  if not p.startswith('/') or '..' in pathlib.PurePosixPath(p).parts or any(ord(c)<32 or c in '\\"' for c in p):raise ValueError('bounded absolute Linux BIF path required')
 attributes=['bootloader, destination_cpu=a53-0','pmufw_image','destination_device=pl','destination_cpu=a53-0, exception_level=el-3, trustzone','load=0x100000, destination_cpu=a53-0','destination_cpu=a53-0, exception_level=el-2']
 return 'the_ROM_image:\n{\n'+''.join(' ['+a+'] "'+p+'"\n' for a,p in zip(attributes,paths))+'}\n'

def _kernel_bound(root,bound,flag):
 bound=pathlib.Path(bound);p=bound/'receipt.json';rec=json.loads(p.read_text());current=snapshot(root)
 if (rec.get('scope')!='ACTUAL_PROVIDER_BUILTIN_KERNEL_AND_BINARY_DT_NOT_COMPLETE_BOOT_IMAGE' or rec.get('natural_exit')!=0 or rec.get('gpu_raw_cycles','missing') is not None or rec.get('provider')!=provider(root,flag) or rec.get('source_before')!=rec.get('source_after') or 'audit' not in rec or 'model_B_kernel_invariance' not in rec):raise ValueError('actual completed matching U23 kernel/DT and model-B invariance required')
 # U24 adds image recipes/build tools only. All executed/runtime and GPU input
 # rows remain exactly those in the actual U22/U23 source cuts.
 adapted=dict(natural_exit=0,target='rv32',target_executed=False,source_before=rec['source_before'],source_after=rec['source_after'],gpu_raw_cycles=None,commands=[])
 require_cross_inputs(adapted,current,'rv32')
 for row in rec['artifacts'].values():
  if sha(row['path'])!=row['sha256']:raise ValueError('compiled U23 artifact changed')
 return rec

def mosaic_payload_policy(env,*,variant=None,fused=None):
 from scripts.unified.mosaic_program import resolve_requested_variant,requires_fusion
 variant=resolve_requested_variant(env,variant=variant,fused=fused);fusion=requires_fusion(variant)
 pointwise=env.get('mosaic_pointwise_budget',4096 if fusion else None);options=env.get('mosaic_profiling_options',0)
 if type(options) is not int or options not in (0,1) or (options and variant not in ('cooperative-v6','reuse-v7','reuse-v8')):raise ValueError('bounded modern Mosaic profile policy required')
 if pointwise is not None and (type(pointwise) is not int or not 1<=pointwise<=4096):raise ValueError('bounded Mosaic pointwise policy required')
 if fusion and pointwise is None:raise ValueError('explicit fused split budget required')
 return dict(variant=variant,fusion=fusion,pointwise_budget=pointwise,profiling_options=options)

def prepare_payload(env_path,flag,bound,out,*,mosaic_variant=None,mosaic_fused=None):
 """Materialize real packages and production files, never execute target ELFs."""
 env=json.loads(pathlib.Path(env_path).read_text());mosaic_policy=mosaic_payload_policy(env,variant=mosaic_variant,fused=mosaic_fused)
 root=pathlib.Path(env['root']).resolve();before=verify_source_lock(root,env['source_lock'],env['source_lock_sha256']);kernel=_kernel_bound(root,bound,flag)
 out=new_build_output(root,out);stage=out/'root';stage.mkdir();records=[]
 build_id='VXU-'+{1:'S8-NN-IMAGE',2:'P8-GRAPHICS',4:'S8-JOINT-NN-IMAGE-P8'}[flag]+'-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
 rec=dict(scope='MATCHED_IMAGE_PAYLOAD_NOT_EXECUTED',provider_flags=flag,build_id=build_id,commands=records,kernel_receipt_sha256=sha(pathlib.Path(bound)/'receipt.json'),source_before=before,gpu_raw_cycles=None,board='NOT_RUN',display='NOT_RUN')
 def put(name,raw,mode=0o444):
  p=stage/name;p.parent.mkdir(parents=True,exist_ok=True)
  with p.open('xb') as f:f.write(raw)
  p.chmod(mode);return p
 def copy(source,name,mode=0o444,expected=None):
  p=pathlib.Path(source)
  if p.is_symlink() or not p.is_file() or (expected is not None and sha(p)!=expected):raise ValueError('exact regular payload input required: '+str(p))
  return put(name,p.read_bytes(),mode)
 def command(argv,label):
  log=out/(label+'.log');begin=time.monotonic()
  with log.open('xb') as f:p=subprocess.run(list(map(str,argv)),cwd=str(root),stdout=f,stderr=subprocess.STDOUT)
  records.append(dict(argv=list(map(str,argv)),natural_exit=p.returncode,log=str(log),sha256=sha(log),elapsed_s=time.monotonic()-begin))
  if p.returncode:raise ValueError('actual HOST package preparation failed: '+label)
  return log.read_text()
 try:
  cross=json.loads(pathlib.Path(env['aarch64_receipt']).read_text())
  if sha(env['aarch64_receipt'])!=env['aarch64_receipt_sha256']:raise ValueError('pinned real AArch64 receipt required')
  require_cross_inputs(cross,before,aarch64_target(flag))
  for item in cross['installation']:
   if item['destination'] not in ('/usr/bin/vxgpud','/usr/bin/vxgpuctl'):raise ValueError('only production daemon/client may install')
   copy(item['source'],item['destination'][1:],0o755,item['sha256'])
  if flag==4:copy(root/'tools/demo.py','usr/bin/demo',0o755)
  declaration=provider(root,flag);firmware=firmware_inputs(root,flag,mosaic_variant=mosaic_policy['variant'])
  if hashlib.sha256(firmware['catalog']).hexdigest()!=kernel['firmware']['catalog-v1.bin']['sha256'] or hashlib.sha256(firmware['execution']).hexdigest()!=kernel['firmware']['execution-v1.bin']['sha256']:raise ValueError('actual kernel DT firmware identity differs')
  for name,raw in [('catalog-v1.bin',firmware['catalog']),('execution-v1.bin',firmware['execution'])]:put('lib/firmware/vxl/'+name,raw)
  for entry in firmware['entries']:put('lib/firmware/vxl/programs/'+entry['entry_id']+'.bin',entry['payload'])
  put('usr/share/vxgpu/catalog.bin',firmware['catalog'])
  hw={k:declaration[k] for k in ('hardware_contract','provider_flags')};hw.update(bit=declaration['bit_sha256'],xsa=declaration['xsa_sha256'],compatible=hashlib.sha256(declaration['compatible'].encode()).hexdigest())
  installation=dict(version=2,hardware=hw,catalog=dict(path='/usr/share/vxgpu/catalog.bin',sha256=hashlib.sha256(firmware['catalog']).hexdigest()),nn=[],display=dict(device='/dev/dri/card0'),image=None,p8=None,mosaic_export=None)
  if flag in (1,4):
   from scripts.unified.generate_hunet_package import generate as hunet
   from scripts.unified.generate_mosaic_package import generate as mosaic
   from scripts.unified.prepare_hunet_view import prepare as hunet_view
   weights=pathlib.Path(env['mosaic_assets']);input_path=pathlib.Path(env['mosaic_input_f32'])
   if sha(input_path)!=env['mosaic_input_sha256'] or input_path.stat().st_size!=1280*960*12:raise ValueError('exact frozen full1280x960 CHW input required')
   models=stage/'usr/share/vxgpu/models';models.mkdir(parents=True)
   rec['generated_models']={'hunet':hunet(env['hunet_weights'],models/'hunet',provider_flag=flag),'mosaic':mosaic(weights,weights.parent/'manifest.json',input_path,1280,960,MOSAIC_IMAGE_BATCH,models/'mosaic',provider_flag=flag,**mosaic_policy)}
   from scripts.unified.bind_kernel import verify_mosaic_firmware_binding
   rec['mosaic_family_binding']=verify_mosaic_firmware_binding(root,firmware,models/'mosaic',mosaic_variant=mosaic_policy['variant'])
   probe=pathlib.Path(env['image_metadata_probe']);raw=probe.read_bytes()
   if sha(probe)!=env['image_metadata_probe_sha256'] or raw[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(raw[18:20],'little')!=62:raise ValueError('genuine HOST-only package metadata probe required, never AArch64 target')
   for kind in ('hunet','mosaic'):
    directory=models/kind
    for p in directory.rglob('*'):
     if p.is_symlink():raise ValueError('model package link rejected')
     p.chmod(0o500 if p.is_dir() else 0o400)
    directory.chmod(0o500)
    result=json.loads(command([probe,kind,directory,'/usr/share/vxgpu/models/'+kind],kind+'-actual-package-prepare'))
    if result.get('scope')!='ACTUAL_HOST_PACKAGE_PREPARATION_DECLARED_CAPS_NOT_DEVICE_OR_GPU' or result.get('gpu_raw_cycles','missing') is not None or result.get('board_identity_verified') is not False:raise ValueError('HOST package metadata scope differs')
    installation['nn'].append(result['installed_model'])
   view=hunet_view(root/'data/demo',stage/'usr/share/vxgpu/hunet-view');installation['display']['hunet']=dict(directory='/usr/share/vxgpu/hunet-view',sha256=view['sha256'])
   table=root/'vendor/image-studio/gpu_programs/tables-64KiB.bin';copy(table,'usr/share/vxgpu/image/tables.bin')
   source_rows=[r for r in before['runtime_files'] if r['path'].startswith(('image/','unified/image_request.cpp','unified/service/native_image_service.cpp'))]
   reference_rows=[r for r in before['runtime_files'] if r['path'].startswith('vendor/image-studio/')]
   if not source_rows or not reference_rows:raise ValueError('actual IMAGE algorithm and frozen reference-source identities required')
   installation['image']=dict(implementation=hashlib.sha256(wire(source_rows)).hexdigest(),reference=hashlib.sha256(wire(reference_rows)).hexdigest(),readonly=dict(path='/usr/share/vxgpu/image/tables.bin',sha256=sha(table)),session=42,bundle='20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3',display_base=100,port=5011,address='0.0.0.0',job_budget_ns=60000000000)
   installation['mosaic_export']=dict(build_id=build_id)
  if flag in (2,4):
   from scripts.unified.cross_assets import verify_original_assets
   assets=verify_original_assets(root);rec['p8_original_assets']=assets
   for row in assets['asset_files']+assets['oracle_files']:copy(root/row['path'],'usr/share/vxgpu/p8/'+pathlib.PurePosixPath(row['path']).name,expected=row['sha256'])
   installation['p8']=dict(assets_directory='/usr/share/vxgpu/p8',assets=assets['p8_asset_id'],session=77,display_base=200,port=5007,address='0.0.0.0',job_budget_ns=60000000000,textures=[])
  config=dict(version=1,private_dir='/run/vxgpud',evidence_dir='/run/vxgpu-evidence',device_path='/dev/vortex0',backend_mode='compute-v2',legacy_profile=0,device_generation=1,**hw)
  put('etc/vxgpud/config.json',wire(config),0o600);raw=wire(installation);put('etc/vxgpud/installation.json',raw,0o400);put('etc/vxgpud/installation.sha256',(hashlib.sha256(raw).hexdigest()+'\n').encode(),0o400)
  put('etc/codex-build-id',(build_id+'\n').encode(),0o644);put('etc/codex-provider-manifest.json',wire(dict(provider=declaration,scope='BOOT_DECLARATION_NOT_INDEPENDENT_PHYSICAL_MEASUREMENT',gpu_raw_cycles=None)),0o644)
  put('etc/default/vxgpud',b'UNIFIED_SERVICE_ENABLED=0\n',0o644);copy(root/'petalinux/unified/service-init','etc/init.d/vxgpud',0o755);copy(root/'petalinux/unified/network-init','etc/init.d/codex-direct-network',0o755);put('etc/network/interfaces',INTERFACES,0o644)
  expected={};entries={}
  for p in sorted(stage.rglob('*')):
   if p.is_dir():
    entries[p.relative_to(stage).as_posix()]=dict(kind='directory',mode=stat.S_IMODE(p.stat().st_mode),uid=0,gid=0)
    continue
   if not p.is_file():continue
   name=p.relative_to(stage).as_posix();mode=stat.S_IMODE(p.stat().st_mode);raw=p.read_bytes();expected[name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),mode=mode);entries[name]=dict(kind='file',mode=mode,uid=0,gid=0,data=raw)
  for path,link in [('etc/rcS.d/S40codex-direct-network','../init.d/codex-direct-network'),('etc/rc5.d/S95vxgpud','../init.d/vxgpud')]:entries[path]=dict(kind='symlink',link=link,uid=0,gid=0,mode=0o777)
  manifest=dict(schema=1,scope=rec['scope'],provider_flags=flag,build_id=build_id,files=expected,gpu_raw_cycles=None)
  errors=payload_errors(entries,manifest)
  if errors:raise ValueError('prepared payload admission: '+repr(errors))
  put('usr/share/vxgpu/payload-manifest.json',wire(manifest));rec.update(payload_manifest_sha256=sha(stage/'usr/share/vxgpu/payload-manifest.json'),payload=manifest,root=str(stage),source_after=snapshot(root),natural_exit=0)
  if rec['source_after']!=before:raise ValueError('frozen source changed during payload preparation')
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

def image_continuation(original,proof,build_log):
 """Only this observed parse-time dependency failure may reuse preparation.

 File SHA/source-lock/provider/resource checks are mandatory in the caller.
 This predicate is never a successful build receipt or general retry waiver.
 """
 if original.get('scope')!='ACTUAL_COMPLETE_PROVIDER_BOOT_CHAIN_BUILD_NOT_BOARD' or type(original.get('natural_exit')) is not int or original['natural_exit']!=1:return False
 if proof.get('scope')!='ACTUAL_UNCHANGED_SOURCE_AFTER_FIRST_IMAGE_DEPENDENCY_PREFLIGHT_ERROR' or type(proof.get('natural_exit')) is not int or proof['natural_exit']!=0 or proof.get('snapshot')!=original.get('source_before') or proof.get('project')!=original.get('project') or not proof.get('project_metadata'):return False
 commands=original.get('commands',[])
 if len(commands)!=2 or any(type(c.get('natural_exit')) is not int for c in commands) or [c['natural_exit'] for c in commands]!=[0,255]:return False
 if not commands[0].get('argv',[''])[-1].endswith('petalinux-config --silentconfig') or not commands[1].get('argv',[''])[-1].endswith('petalinux-build'):return False
 if "ERROR: Nothing RPROVIDES 'python3-subprocess'" not in build_log or re.search(r'(?m)^(?:NOTE: )?(?:Executing Tasks|Running task)',build_log):return False
 return True

def image_bootgen_reprepare(original,proof,build_log):
 """Only the observed build0/package0/ambient-bootgen127 may reprepare.

 A fresh image15GiB gate, whole unchanged-source proof, pinned first receipts,
 verified A metadata capsule, same private provider/project and complete new
 payload remain mandatory in prepare_image. This never relabels old failure.
 """
 if original.get('scope')!='ACTUAL_COMPLETE_PROVIDER_BOOT_CHAIN_BUILD_NOT_BOARD' or type(original.get('natural_exit')) is not int or original['natural_exit']!=1:return False
 if proof.get('scope')!='ACTUAL_UNCHANGED_SOURCE_AFTER_BOOTGEN_PATH_ERROR' or type(proof.get('natural_exit')) is not int or proof['natural_exit']!=0 or proof.get('project')!=original.get('project') or not proof.get('project_metadata') or wire(proof.get('snapshot'))!=wire(original.get('source_before')):return False
 commands=original.get('commands',[])
 if len(commands)!=3 or any(type(c.get('natural_exit')) is not int for c in commands) or [c['natural_exit'] for c in commands]!=[0,0,127]:return False
 tokens=('petalinux-build','petalinux-package --boot','bootgen -arch zynqmp')
 if any(not isinstance(c.get('argv'),list) or not c['argv'] or not isinstance(c['argv'][-1],str) or token not in c['argv'][-1] for c,token in zip(commands,tokens)):return False
 if not commands[0]['argv'][-1].endswith('petalinux-build') or 'bash: bootgen: command not found' not in build_log:return False
 return True

def image_policy(previous,policy,continuation=False):
 if 'python unified_startup_policy()' in previous:
  if not continuation or previous.count('python unified_startup_policy()')!=1 or not previous.endswith('\n'+policy):raise ValueError('existing image policy differs or continuation was not validated')
  return previous
 return previous+'\n'+policy

def prepare_image(env_path,flag,bound,payload_path,out,resume_receipt=None,source_after=None):
 """Continue the exact completed U23 project into the complete boot chain.

 Caller must hold the existing C three-lock lease. The fresh original15GiB
 resource gate is retained, even though this resumes an existing project.
 """
 env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(env['root']).resolve();before=verify_source_lock(root,env['source_lock'],env['source_lock_sha256']);kernel=_kernel_bound(root,bound,flag)
 from scripts.demo.image import resource_facts,monitored
 from scripts.unified.resources import resource_gate
 project=pathlib.Path(kernel['project']).resolve();resources=resource_gate(resource_facts(project),'image')
 if resources['required_disk']!=15*1024**3 or resources['worker_count']!=1:raise ValueError('original image15GiB/worker1 gate required')
 payload_path=pathlib.Path(payload_path).resolve();payload=json.loads((payload_path/'receipt.json').read_text());stage=pathlib.Path(payload['root']).resolve()
 if payload.get('natural_exit')!=0 or payload.get('provider_flags')!=flag or payload.get('gpu_raw_cycles','missing') is not None or payload['source_before']!=before or payload['source_after']!=before or sha(pathlib.Path(bound)/'receipt.json')!=payload['kernel_receipt_sha256']:raise ValueError('actual matching completed payload and original kernel receipt required')
 continuation=None
 if resume_receipt:
  if not source_after:raise ValueError('pinned actual source-after proof required before image continuation')
  for path,key in [(resume_receipt,'image_resume_receipt_sha256'),(source_after,'image_source_after_receipt_sha256')]:
   expected=env.get(key)
   if not isinstance(expected,str) or not re.fullmatch('[0-9a-f]{64}',expected) or sha(path)!=expected:raise ValueError('actual failed image/source-after receipts must match pinned SHA')
  original=json.loads(pathlib.Path(resume_receipt).read_text());proof=json.loads(pathlib.Path(source_after).read_text());log=pathlib.Path(original['commands'][-1]['log'])
  repair_kind='DEPENDENCY_PREFLIGHT' if image_continuation(original,proof,log.read_text(errors='replace')) else 'BOOTGEN_PATH' if image_bootgen_reprepare(original,proof,log.read_text(errors='replace')) else None
  if repair_kind is None or proof.get('original_receipt_sha256')!=sha(resume_receipt) or original.get('provider')!=kernel['provider'] or pathlib.Path(original['project']).resolve()!=project or original.get('kernel_receipt_sha256')!=sha(pathlib.Path(bound)/'receipt.json') or original.get('gpu_raw_cycles','missing') is not None:raise ValueError('only exact actual unchanged known preparation error may be repaired')
  for c in original['commands']:
   if sha(c['log'])!=c['log_sha256']:raise ValueError('first failed image command log changed')
  archive=env.get('image_original_metadata_archive',{})
  if not re.fullmatch('[0-9a-f]{64}',archive.get('sha256','')):raise ValueError('whole first prepared metadata capsule SHA required')
  if repair_kind=='DEPENDENCY_PREFLIGHT':
   if archive.get('verified_members')!=28:raise ValueError('complete original parse-failure metadata capsule required')
  elif archive.get('verified_members')!=20 or archive.get('natural_exit')!=0 or archive.get('project_metadata_digest')!=hashlib.sha256(wire(proof['project_metadata'])).hexdigest():raise ValueError('actual complete20-member A capsule and all16 original metadata rows must be verified before bootgen repair')
  for row in proof['project_metadata']:
   p=pathlib.Path(row['path'])
   if project not in p.parents or p.is_symlink() or not p.is_file() or p.stat().st_size!=row['bytes'] or sha(p)!=row['sha256']:raise ValueError('first prepared image metadata changed before guarded repair')
  adapted=dict(natural_exit=0,target='rv32',target_executed=False,source_before=original['source_before'],source_after=proof['snapshot'],commands=[],gpu_raw_cycles=None);require_cross_inputs(adapted,before,'rv32')
  continuation=dict(scope='EXACT_KNOWN_PREPARATION_REPAIR_NOT_PRIOR_BUILD_PASS',repair_kind=repair_kind,failed_receipt=dict(path=str(resume_receipt),sha256=sha(resume_receipt),natural_exit=1),source_after=dict(path=str(source_after),sha256=sha(source_after)),first_metadata_archive=archive,old_source_snapshot_id=original['source_before']['source_snapshot_id'])
 elif source_after:raise ValueError('source-after requires explicit failed image receipt')
 out=new_build_output(root,out);meta=project/'project-spec/meta-user';commands=[]
 rec=dict(scope='ACTUAL_COMPLETE_PROVIDER_BOOT_CHAIN_BUILD_NOT_BOARD',project=str(project),provider=provider(root,flag),resources=resources,source_before=before,kernel_receipt_sha256=sha(pathlib.Path(bound)/'receipt.json'),payload_receipt_sha256=sha(payload_path/'receipt.json'),commands=commands,build_id=payload['build_id'],gpu_raw_cycles=None,board='NOT_RUN',sd='NOT_TOUCHED')
 if continuation:rec['continued_from']=continuation
 def write(relative,raw):
  p=meta/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw if isinstance(raw,bytes) else raw.encode());return p
 def shell(argv,label):
  log=out/(label+'.log');cmd=['bash','-c','set -e; . '+shlex.quote(env['petalinux_settings'])+'; '+' '.join(shlex.quote(str(x)) for x in argv)];begin=time.monotonic()
  try:monitored(cmd,project,log,project)
  finally:
   code=pathlib.Path(str(log)+'.exit');commands.append(dict(argv=cmd,natural_exit=int(code.read_text()) if code.is_file() else None,elapsed_s=time.monotonic()-begin,log=str(log),log_sha256=sha(log) if log.is_file() else None));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
  if re.search(r'(?m)^\s*(?:ERROR:|FATAL:|Traceback \()',log.read_text(errors='replace')):raise ValueError('fatal/error build semantics retained: '+label)
 try:
  owned={'etc/codex-build-id','etc/codex-provider-manifest.json','etc/init.d/codex-direct-network','etc/network/interfaces'}
  for package,template in [('unified-runtime','runtime.bb'),('unified-assets','assets.bb')]:
   write('recipes-apps/'+package+'/'+package+'.bb',(root/'petalinux/unified'/template).read_bytes());archive=meta/'recipes-apps'/package/'files'/(package+'.tar.gz');archive.parent.mkdir(parents=True,exist_ok=True)
   with tarfile.open(str(archive),'w:gz',compresslevel=1) as t:
    directories=set()
    for name in payload['payload']['files']:
     runtime=name.startswith(('etc/','usr/bin/')) or name=='usr/share/vxgpu/payload-manifest.json'
     if name in owned or (package=='unified-runtime')!=runtime:continue
     directories.update(str(p) for p in pathlib.PurePosixPath(name).parents if str(p)!='.')
    if package=='unified-assets':
     models=stage/'usr/share/vxgpu/models'
     if models.is_symlink():raise ValueError('model directory link rejected before recipe packing')
     if models.is_dir():
      directories.update(p.relative_to(stage).as_posix() for p in models.rglob('*') if p.is_dir() or p.is_symlink())
    directory_members=[]
    for name in sorted(directories,key=lambda n:(len(pathlib.PurePosixPath(n).parts),n),reverse=True):
     p=stage/name;state=p.lstat();mode=stat.S_IMODE(state.st_mode)
     if not stat.S_ISDIR(state.st_mode):raise ValueError('directory changed before recipe packing: '+name)
     if name.startswith('usr/share/vxgpu/models/') and (mode&0o7222 or mode&0o500!=0o500):raise ValueError('model directory must remain read/search-only before recipe packing: '+name)
     info=tarfile.TarInfo(name);info.type=tarfile.DIRTYPE;info.mode=mode;info.uid=info.gid=0;directory_members.append(info)
    for name,row in sorted(payload['payload']['files'].items()):
     if name in owned:continue
     runtime=name.startswith(('etc/','usr/bin/')) or name=='usr/share/vxgpu/payload-manifest.json'
     if (package=='unified-runtime')!=runtime:continue
     p=stage/name
     if sha(p)!=row['sha256'] or p.stat().st_size!=row['bytes'] or stat.S_IMODE(p.stat().st_mode)!=row['mode']:raise ValueError('payload changed before recipe packing')
     info=tarfile.TarInfo(name);info.size=row['bytes'];info.mode=row['mode'];info.uid=info.gid=0
     with p.open('rb') as f:t.addfile(info,f)
    # The manifest inventories all other files; it cannot hash itself.
    if package=='unified-runtime':
     p=stage/'usr/share/vxgpu/payload-manifest.json';info=tarfile.TarInfo('usr/share/vxgpu/payload-manifest.json');info.size=p.stat().st_size;info.mode=0o444
     with p.open('rb') as f:t.addfile(info,f)
    # Seal directories only after writing every descendant. Normal non-root
    # tar extraction may restore a directory as soon as it leaves that group;
    # an early 0500 header would then block files encountered later in the tar.
    # Deepest-first final metadata needs no extractor-specific chmod override.
    for info in directory_members:t.addfile(info)
  # Reuse each existing recipe's file ownership rather than installing a second
  # package over firstboot or init-ifupdown paths (RPM conflict is not ignored).
  write('recipes-apps/codex-firstboot/codex-firstboot.bb',(root/'petalinux/unified/firstboot.bb').read_bytes())
  for name in ('codex-build-id','codex-provider-manifest.json'):write('recipes-apps/codex-firstboot/files/'+name,(stage/'etc'/name).read_bytes())
  write('recipes-apps/codex-firstboot/files/codex-direct-network',(stage/'etc/init.d/codex-direct-network').read_bytes())
  write('recipes-core/init-ifupdown/files/interfaces',INTERFACES);write('recipes-core/init-ifupdown/init-ifupdown_%.bbappend','FILESEXTRAPATHS_prepend := "${THISDIR}/files:"\nSRC_URI += " file://interfaces"\n')
  rootfs=project/'project-spec/configs/rootfs_config';text=rootfs.read_text()
  for name in ('unified-runtime','unified-assets'):
   text=re.sub(r'(?m)^(?:# )?CONFIG_'+re.escape(name)+r'(?:=.*| is not set)$','',text);text+='\nCONFIG_'+name+'=y\n'
  rootfs.write_text(text)
  user=meta/'conf/user-rootfsconfig'
  prior=user.read_text() if user.is_file() else ''
  with user.open('a') as f:
   for line in ('CONFIG_unified-runtime','CONFIG_unified-assets'):
    if line not in prior.splitlines():f.write('\n'+line+'\n')
  # Disable only stale competing autostarts in this new generated rootfs.
  policy='''
python unified_startup_policy() {
    import os,re
    from pathlib import Path
    root=Path(d.getVar('IMAGE_ROOTFS')).resolve()
    for pattern in ('etc/rcS.d/S*','etc/rc[2-5].d/S*'):
        for p in root.glob(pattern):
            if re.search(r'(?:demo-viewer|vxl-demo|board[-_]check|check[-_]board|alinx)',p.name,re.I):
                if not p.is_symlink():
                    bb.fatal('unexpected non-symlink legacy startup: '+str(p))
                p.unlink()
}
ROOTFS_POSTPROCESS_COMMAND += " unified_startup_policy; "
'''
  image_append=meta/'recipes-core/images/petalinux-image-minimal.bbappend'
  previous=image_append.read_text() if image_append.is_file() else ''
  write('recipes-core/images/petalinux-image-minimal.bbappend',image_policy(previous,policy,continuation is not None))
  shell(['petalinux-config','--silentconfig'],'configure-complete-rootfs')
  shell(['petalinux-build'],'build-complete-boot-chain-rootfs-fit')
  im=project/'images/linux';components={n:dict(path=str(im/n),bytes=(im/n).stat().st_size,sha256=sha(im/n)) for n in ('zynqmp_fsbl.elf','pmufw.elf','bl31.elf','u-boot.elf')};bit=env['provider_hardware'][str(flag)]['bit']
  if sha(bit)!=rec['provider']['bit_sha256']:raise ValueError('exact selected provider bit changed before boot packaging')
  shell(['petalinux-package','--boot','--fsbl',im/'zynqmp_fsbl.elf','--fpga',bit,'--u-boot',im/'u-boot.elf','--pmufw',im/'pmufw.elf','--atf',im/'bl31.elf','--dtb',im/'system.dtb','--force','--output',im/'BOOT.BIN'],'package-exact-new-components')
  # Independently reassemble from an explicit pinned BIF and compare every byte.
  bif=out/'verification.bif';bif.write_text(boot_bif(im,bit))
  tool=bootgen_tool(env['petalinux_settings'],env['bootgen_sha256']);rec['bootgen_tool']=dict(path=str(tool),sha256=sha(tool));shell([tool,'-arch','zynqmp','-image',bif,'-o',out/'verification.BOOT.BIN','-w'],'independent-complete-boot-reassembly')
  if sha(im/'BOOT.BIN')!=sha(out/'verification.BOOT.BIN'):raise ValueError('independent complete BOOT reassembly differs; preserve both first outputs')
  kernel_build=pathlib.Path(kernel['kernel_build']);mkimage=kernel_build.parent/'recipe-sysroot-native/usr/bin/mkimage';bootcmd=out/'boot.cmd';bootcmd.write_bytes(BOOT_TEXT)
  shell([mkimage,'-A','arm','-T','script','-C','none','-n',payload['build_id'],'-d',bootcmd,im/'boot.scr'],'package-bounded-sd-boot-script')
  rec.update(components=components,boot_reassembly=dict(bif_sha256=sha(bif),boot_sha256=sha(im/'BOOT.BIN'),independent_sha256=sha(out/'verification.BOOT.BIN')),source_after=snapshot(root),natural_exit=0)
  if rec['source_after']!=before:raise ValueError('frozen source changed during complete image build')
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('phase',choices=('payload','build','audit'));p.add_argument('--env',required=True);p.add_argument('--provider',required=True,type=int,choices=(1,2,4));p.add_argument('--bound',required=True);p.add_argument('--out',required=True);p.add_argument('--payload');p.add_argument('--image-receipt');p.add_argument('--resume-existing');p.add_argument('--source-after');p.add_argument('--mosaic-variant');a=p.parse_args()
 if a.mosaic_variant and a.phase!='payload':p.error('--mosaic-variant applies to payload generation; later phases retain that payload identity')
 if a.phase!='build' and (a.resume_existing or a.source_after):p.error('continuation applies only to a complete-image build')
 if a.phase=='payload':result=prepare_payload(a.env,a.provider,a.bound,a.out,mosaic_variant=a.mosaic_variant)
 elif a.phase=='build':
  if not a.payload:p.error('--payload is required for an actual complete image build')
  result=prepare_image(a.env,a.provider,a.bound,a.payload,a.out,a.resume_existing,a.source_after)
 else:
  if not a.payload or not a.image_receipt:p.error('--payload and --image-receipt are required for actual content audit')
  from scripts.unified.audit_image import audit_image
  result=audit_image(a.env,a.bound,a.payload,a.image_receipt,a.out)
 print(json.dumps(dict(scope=result['scope'],natural_exit=result['natural_exit'],build_id=result.get('build_id'),gpu_raw_cycles=None,board='NOT_RUN',sd='NOT_TOUCHED')))
