"""Private U23 input binding; no board or media APIs."""
import hashlib,json,pathlib,re,struct,os,shutil,tarfile,shlex,stat
from scripts.unified.cross_common import aarch64_target,validate_provider_identity,provider,verify_source_lock,new_build_output,sha,snapshot
from scripts.unified.build_catalog import build_catalog

ABSENT=0xffffffff
WATCHDOG=4000000000

def component_continuation(original,source_after):
 if (original.get('scope')!='ACTUAL_PROVIDER_BUILTIN_KERNEL_AND_BINARY_DT_NOT_COMPLETE_BOOT_IMAGE' or type(original.get('natural_exit')) is not int or original['natural_exit']!=1 or not str(original.get('error','')).startswith('FileNotFoundError(') or set(original.get('artifacts',{}))!={'vmlinux','Image'}):raise ValueError('only known post-build missing monolithic-object layout error may continue')
 commands=original.get('commands',[])
 if len(commands)!=3 or any(type(c.get('natural_exit')) is not int or c['natural_exit']!=0 for c in commands):raise ValueError('all actual import/kernel/DT commands must have succeeded; never promote failed compilation')
 for row,token in zip(commands,('petalinux-config --get-hw-description=','petalinux-build -c kernel','petalinux-build -c device-tree')):
  if not any(isinstance(arg,str) and token in arg for arg in row.get('argv',[])):raise ValueError('actual component command sequence differs')
 if (source_after.get('scope')!='ACTUAL_UNCHANGED_SOURCE_AFTER_ORIGINAL_COMPONENT_BUILD' or type(source_after.get('natural_exit')) is not int or source_after['natural_exit']!=0 or source_after.get('snapshot')!=original.get('source_before') or source_after.get('missing_monolithic_object') is not True or source_after.get('actual_builtin_archive') is not True):raise ValueError('original frozen source and actual built-in layout observation required after natural exit')
 return True

def builtin_bundle_inputs(kernel_build,member_paths):
 kb=pathlib.Path(kernel_build).resolve();directory=kb/'drivers/misc/vxl/driver'
 if type(member_paths) is not list or not 1<=len(member_paths)<=512 or any(type(p) is not str for p in member_paths) or len(set(member_paths))!=len(member_paths):raise ValueError('bounded unique complete built-in archive member list required')
 rows=[];names=set();total=0
 for name in member_paths:
  p=pathlib.Path(name)
  if not p.is_absolute() or p.resolve()!=p or directory not in p.parents or p.suffix!='.o':raise ValueError('actual private built-in object path escapes driver directory')
  before=p.lstat()
  if not stat.S_ISREG(before.st_mode) or not 64<=before.st_size<=64*1024**2:raise ValueError('actual bounded regular built-in object required')
  with p.open('rb') as f:header=f.read(64)
  if header[:7]!=b'\x7fELF\x02\x01\x01' or struct.unpack_from('<HH',header,16)!=(1,183):raise ValueError('actual AArch64 relocatable built-in input required')
  digest=sha(p);after=p.lstat()
  if (before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns):raise ValueError('compiled object mutated during admission')
  relative=p.relative_to(directory).as_posix();names.add(relative);total+=before.st_size;rows.append(dict(path=str(p),relative=relative,bytes=before.st_size,sha256=digest))
 if not {'vxl_drv.o','v2/startup_linux.o','shared_v2_owner.o'}<=names or total>256*1024**2:raise ValueError('complete builtin/boot/owner paths and bounded input bytes required')
 return rows

def bundle_builtin_driver(kernel_build,prefix,out):
 """Link only actual closed built-in.a inputs into an audit-only REL ELF.

 Linux5.4 bool composites are flattened into a thin built-in.a. A module-style
 vxl_linux.o is not a real builtin output. This bundle is not loaded or placed
 back into the kernel; the actual vmlinux remains the execution artifact.
 """
 import subprocess,time
 kb=pathlib.Path(kernel_build).resolve();out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False);archive=kb/'drivers/misc/vxl/driver/built-in.a'
 if archive.is_symlink() or not archive.is_file() or archive.read_bytes()[:8]!=b'!<thin>\n':raise ValueError('actual Linux5.4 private built-in thin archive required')
 archive_sha=sha(archive);commands=[]
 def run(argv,name):
  log=out/(name+'.log');begin=time.monotonic()
  with log.open('xb') as f:p=subprocess.run(argv,cwd=str(kb),stdout=f,stderr=subprocess.STDOUT)
  commands.append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),log_sha256=sha(log)))
  if p.returncode:raise ValueError('actual archive/relocatable audit command failed: '+name)
  return log.read_text()
 rows=builtin_bundle_inputs(kb,run([prefix+'ar','t',str(archive)],'actual-builtin-members').splitlines())
 target=out/'builtin-driver.o';run([prefix+'ld','-r','--whole-archive',str(archive),'--no-whole-archive','-o',str(target)],'derive-all-builtin-inputs-relocatable')
 if sha(archive)!=archive_sha or any(sha(r['path'])!=r['sha256'] for r in rows):raise ValueError('actual built-in archive or inputs changed while linking audit bundle')
 result=dict(scope='ACTUAL_BUILTIN_ARCHIVE_ALL_MEMBER_RELOCATABLE_EXTRACTION_NOT_MODULE_OR_GPU',archive=dict(path=str(archive),sha256=archive_sha,bytes=archive.stat().st_size),members=rows,commands=commands,artifact=dict(path=str(target),bytes=target.stat().st_size,sha256=sha(target)),gpu_raw_cycles=None)
 (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');return result

def require_cross_inputs(receipt,current,target):
 """Permit documented package/build-tool evolution, never changed execution bytes."""
 canonical=lambda value:json.dumps(value,sort_keys=True,separators=(',',':'))
 if type(receipt.get('natural_exit')) is not int or receipt['natural_exit']!=0 or receipt.get('target')!=target or receipt.get('gpu_raw_cycles','missing') is not None:raise ValueError('matching successful non-executed cross receipt required')
 if receipt.get('target_executed' if target=='rv32' else 'executed_target_elfs') is not False:raise ValueError('unexecuted offline target scope required')
 old=receipt.get('source_before')
 if not isinstance(old,dict) or canonical(old)!=canonical(receipt.get('source_after')):raise ValueError('cross source changed during compilation')
 if old.get('runtime_source_id')!=current.get('runtime_source_id') or canonical(old.get('runtime_files'))!=canonical(current.get('runtime_files')):raise ValueError('runtime/driver/operator inputs differ from actual cross build')
 # GPU program source and flat payloads have their own identity domain, in
 # addition to the user-space source domain. Model packages are separate.
 prefixes=('device/','kernel/','data/nnrt/programs/','data/image/','data/p8/')
 def program_rows(value):
  rows=value.get('files')
  if type(rows) is not list or any(type(r) is not dict or type(r.get('path')) is not str for r in rows) or len({r['path'] for r in rows})!=len(rows):raise ValueError('unique source manifest rows required')
  return [r for r in rows if r['path'].startswith(prefixes)]
 if canonical(program_rows(old))!=canonical(program_rows(current)):raise ValueError('GPU source/program/asset inputs differ from actual cross build')
 if any(c.get('natural_exit')!=0 for c in receipt.get('commands',[])):raise ValueError('cross command failed')
 return None

def device_tree_recipe(parent):
 """Preserve PetaLinux's real soc_mapping bridge and attach both user inputs.

 SRC_URI alone does not include system-user.dtsi into DTG system-top.dts.
 The parent bridge also generates system-conf.dtsi from actual PS settings.
 """
 if not isinstance(parent,bytes) or not 1<=len(parent)<=65536 or b'VX_UNIFIED_DTG_RECIPE_V1' in parent or b'petalinux_hsm_bridge.tcl' not in parent or b'soc_mapping' not in parent or b'file://system-user.dtsi' not in parent:raise ValueError('actual unmodified parent soc_mapping DT recipe required')
 return parent+b'''\n# VX_UNIFIED_DTG_RECIPE_V1 -- retain the original soc_mapping bridge above.
SRC_URI += " file://unified-identity.dtsi"
do_configure_append () {
    install -m 0644 ${WORKDIR}/system-user.dtsi ${DT_FILES_PATH}/system-user.dtsi
    install -m 0644 ${WORKDIR}/unified-identity.dtsi ${DT_FILES_PATH}/unified-identity.dtsi
    test -s ${DT_FILES_PATH}/system-conf.dtsi
    grep -q 'system-user.dtsi' ${DT_FILES_PATH}/system-top.dts || printf '\\n/include/ "system-user.dtsi"\\n' >> ${DT_FILES_PATH}/system-top.dts
}
'''

def copy_project_metadata(old,project):
 """Copy configuration only; never reuse another project's writable build."""
 old=pathlib.Path(old);project=pathlib.Path(project)
 marker=old/'config.project'
 if marker.is_symlink() or not marker.is_file() or not 1<=marker.stat().st_size<=65536:raise ValueError('actual bounded regular PetaLinux config.project required')
 for item in old.iterdir():
  if item.name in ('project-spec','.petalinux') and item.is_dir():shutil.copytree(str(item),str(project/item.name),symlinks=True)
  elif item.is_file() and not item.is_symlink() and item.name in ('config.project','Makefile','.gitignore','.project','.cproject'):shutil.copy2(str(item),str(project/item.name))

def build_kernel(env_path,provider_flag,source_lock_path,out,*,mosaic_variant=None,mosaic_fused=None):
 """Real provider-specific kernel/DT component build in a fresh PetaLinux tree.

 The caller holds the existing three coordination locks, with worker1. Reuse
 only downloads/sstate; never share a writable KS/KB or an existing BOOT/FIT.
 U24 continues this exact project for the rest of the complete boot chain.
 """
 env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(env['root']).resolve()
 from scripts.unified.mosaic_program import resolve_requested_variant
 mosaic_variant=resolve_requested_variant(env,variant=mosaic_variant,fused=mosaic_fused)
 before=verify_source_lock(root,source_lock_path,env['source_lock_sha256'])
 declaration=provider(root,provider_flag)
 from scripts.unified.audit_kernel import read_hardware_input,audit_kernel
 hardware=env['provider_hardware'][str(provider_flag)]
 hardware_id=read_hardware_input(root,provider_flag,hardware['xsa'],hardware['bit'])
 for key,target in [('rv32_receipt','rv32'),('aarch64_receipt',aarch64_target(provider_flag))]:
  receipt_path=pathlib.Path(env[key]);expected=env[key+'_sha256']
  if not isinstance(expected,str) or not re.fullmatch('[0-9a-f]{64}',expected) or sha(receipt_path)!=expected:raise ValueError('actual completed U22 receipt digest required')
  receipt=json.loads(receipt_path.read_text());require_cross_inputs(receipt,before,target)
 old=pathlib.Path(env['image_parent']).resolve();settings=pathlib.Path(env['petalinux_settings']).resolve()
 if not old.is_dir() or not (old/'project-spec/configs/config').is_file() or not settings.is_file():raise ValueError('actual PetaLinux2020.1 parent/settings required')
 from scripts.demo.image import resource_facts,monitored
 from scripts.unified.resources import resource_gate,build_output_resource_path
 resources=resource_gate(resource_facts(build_output_resource_path(root,out)),'image')
 if resources['required_disk']!=15*1024**3 or resources['worker_count']!=1:raise ValueError('original image15GiB and worker1 gate required')
 out=new_build_output(root,out);project=out/'project';project.mkdir();commands=[]
 rec=dict(scope='ACTUAL_PROVIDER_BUILTIN_KERNEL_AND_BINARY_DT_NOT_COMPLETE_BOOT_IMAGE',provider=declaration,hardware=hardware_id,source_before=before,source_lock=dict(path=str(source_lock_path),sha256=env['source_lock_sha256']),parent=str(old),project=str(project),resources=resources,commands=commands,artifacts={},gpu_raw_cycles=None,board='NOT_RUN',sd='NOT_TOUCHED',image='U24_COMPLETE_BOOT_CHAIN_PENDING',compiler_jobs=1)
 def write(relative,data):
  p=project/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data if isinstance(data,bytes) else data.encode());return p
 def evidence(path):
  path=pathlib.Path(path);s=path.lstat()
  if not stat.S_ISREG(s.st_mode) or s.st_size==0:raise ValueError('actual regular nonempty artifact required: '+str(path))
  return dict(path=str(path),bytes=s.st_size,sha256=sha(path))
 def shell(argv,label):
  import time
  log=out/(label+'.log');cmd=['bash','-c','set -e; . '+shlex.quote(str(settings))+'; '+' '.join(shlex.quote(str(v)) for v in argv)];start=time.monotonic()
  try:monitored(cmd,project,log,project)
  finally:
   code=int(pathlib.Path(str(log)+'.exit').read_text()) if pathlib.Path(str(log)+'.exit').is_file() else None
   commands.append(dict(argv=cmd,natural_exit=code,elapsed_s=time.monotonic()-start,log=str(log),log_sha256=sha(log) if log.is_file() else None));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
  if re.search(r'(?m)^\s*(?:ERROR:|FATAL:|Traceback \()',log.read_text(errors='replace')):raise ValueError('build log contains fatal/error semantics despite shell result')
 try:
  # Copy only parent configuration/source metadata, never old build/images.
  copy_project_metadata(old,project)
  (project/'build').mkdir()
  for name in ('downloads','sstate-cache'):
   cache=(old/'build'/name).resolve()
   if not cache.is_dir():raise ValueError('original explicitly shared cache missing: '+name)
   (project/'build'/name).symlink_to(cache,target_is_directory=True)
  firmware=firmware_inputs(root,provider_flag,mosaic_variant=mosaic_variant);release=out/'firmware';programs=release/'programs';programs.mkdir(parents=True)
  (release/'catalog-v1.bin').write_bytes(firmware['catalog']);(release/'execution-v1.bin').write_bytes(firmware['execution'])
  for entry in firmware['entries']:(programs/(entry['entry_id']+'.bin')).write_bytes(entry['payload'])
  rec['firmware']={p.relative_to(release).as_posix():evidence(p) for p in release.rglob('*') if p.is_file()}
  rec['execution_header_version']=struct.unpack_from('<I',firmware['execution'],4)[0]
  # Preserve old release headers byte for byte. They are legacy binding input,
  # separate from the new root-owned program catalog read at v2 startup.
  parent_driver=pathlib.Path(env['kernel_source'])/'drivers/misc/vxl/driver';headers={}
  for p in parent_driver.iterdir():
   if p.is_file() and (p.name.endswith('_release.h') or p.name=='vxl_hunet_source.h'):
    if (root/'driver'/p.name).is_file() and (root/'driver'/p.name).read_bytes()!=p.read_bytes():raise ValueError('legacy header conflict; no implicit identity rewrite: '+p.name)
    headers[p.name]=evidence(p)
  if 'vxl_s8_release.h' not in headers or 'vxl_mosaic_release.h' not in headers:raise ValueError('actual legacy release identity headers required')
  packet=project/'project-spec/meta-user/recipes-kernel/linux/files/vxl-unified-source.tar.gz';packet.parent.mkdir(parents=True,exist_ok=True);kernel_inputs=[]
  with tarfile.open(str(packet),'w:gz',compresslevel=1) as archive:
   paths=[]
   for name in ('driver','core','include','generated'):
    paths.extend(p for p in (root/name).rglob('*') if p.is_file() and p.suffix not in ('.o','.ko','.pyc') and '__pycache__' not in p.parts)
   for p in sorted(paths):
    if p.is_symlink():raise ValueError('kernel source packet cannot follow symlinks')
    relative=p.relative_to(root).as_posix();raw=builtin_makefile(p.read_bytes()) if relative=='driver/Makefile' else p.read_bytes();info=tarfile.TarInfo(relative);info.size=len(raw);info.mode=0o644
    import io
    archive.addfile(info,io.BytesIO(raw));kernel_inputs.append(dict(path=relative,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
   for name,row in sorted(headers.items()):
    if (root/'driver'/name).is_file():continue
    p=pathlib.Path(row['path']);info=tarfile.TarInfo('driver/'+name);info.size=p.stat().st_size;info.mode=0o644
    with p.open('rb') as f:archive.addfile(info,f)
    kernel_inputs.append(dict(path='driver/'+name,bytes=row['bytes'],sha256=row['sha256']))
  rec.update(kernel_input_files=sorted(kernel_inputs,key=lambda r:r['path']),legacy_headers=headers,source_packet=evidence(packet))
  recipe='''FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += " file://vxl-unified-source.tar.gz;subdir=vxl-source file://unified.cfg"
do_patch_append() {
    install -d ${S}/drivers/misc/vxl
    cp -a ${WORKDIR}/vxl-source/driver ${WORKDIR}/vxl-source/core ${WORKDIR}/vxl-source/include ${WORKDIR}/vxl-source/generated ${S}/drivers/misc/vxl/
    grep -q 'vxl/driver/' ${S}/drivers/misc/Makefile || printf '\\nobj-$(CONFIG_VXL_LINUX) += vxl/driver/\\n' >> ${S}/drivers/misc/Makefile
    grep -q 'vxl/driver/Kconfig' ${S}/drivers/misc/Kconfig || printf '\\nsource "drivers/misc/vxl/driver/Kconfig"\\n' >> ${S}/drivers/misc/Kconfig
}
'''
  write('project-spec/meta-user/recipes-kernel/linux/linux-xlnx_%.bbappend',recipe)
  write('project-spec/meta-user/recipes-kernel/linux/files/unified.cfg',(root/'petalinux/unified/kernel.fragment').read_bytes())
  write('project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend',device_tree_recipe((old/'project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend').read_bytes()))
  write('project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi',(root/'petalinux/unified'/('s8.dtsi' if provider_flag in (1,4) else 'p8.dtsi')).read_bytes())
  write('project-spec/meta-user/recipes-bsp/device-tree/files/unified-identity.dtsi',identity_dtsi(declaration,sha(release/'catalog-v1.bin'),sha(release/'execution-v1.bin')))
  conf=project/'project-spec/meta-user/conf/petalinuxbsp.conf'
  with conf.open('a') as f:f.write('\n# Unified candidate, original C locks held, one worker\nBB_NO_NETWORK = "1"\nBB_NUMBER_THREADS = "1"\nPARALLEL_MAKE = "-j1"\nBB_DISKMON_DIRS = "STOPTASKS,${TMPDIR},2200M,100K"\n')
  hw=project/'unified-hardware';hw.mkdir();shutil.copyfile(str(hardware['xsa']),str(hw/'system.xsa'))
  if sha(hw/'system.xsa')!=declaration['xsa_sha256']:raise ValueError('exact matched XSA copy differs')
  shell(['petalinux-config','--get-hw-description='+str(hw),'--silentconfig'],'import-provider-xsa')
  shell(['petalinux-build','-c','kernel'],'build-actual-builtin-kernel')
  shell(['petalinux-build','-c','device-tree'],'build-actual-provider-dtb')
  builds=list((project/'build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx').glob('*/linux-zynqmp_generic-standard-build'))
  if len(builds)!=1:raise ValueError('unique new private kernel build directory required')
  kb=builds[0].resolve();ks=(project/'build/tmp/work-shared/zynqmp-generic/kernel-source').resolve()
  if project not in kb.parents or project not in ks.parents:raise ValueError('provider KS/KB escaped its private project')
  rec.update(kernel_source=str(ks),kernel_build=str(kb))
  bundle=bundle_builtin_driver(kb,env['aarch64_prefix'],out/'builtin-driver-evidence');rec['builtin_bundle']=bundle;commands.extend(bundle['commands'])
  for key,p in [('vmlinux',kb/'vmlinux'),('Image',kb/'arch/arm64/boot/Image'),('driver',pathlib.Path(bundle['artifact']['path'])),('config',kb/'.config'),('symvers',kb/'Module.symvers'),('dtb',project/'images/linux/system.dtb')]:rec['artifacts'][key]=evidence(p)
  prefix=env['aarch64_prefix'];nm=out/'vmlinux.nm';import subprocess
  with nm.open('xb') as f:proc=subprocess.run([prefix+'nm','--defined-only',str(kb/'vmlinux')],stdout=f,stderr=subprocess.STDOUT)
  commands.append(dict(argv=[prefix+'nm','--defined-only',str(kb/'vmlinux')],natural_exit=proc.returncode,log=str(nm),log_sha256=sha(nm)))
  if proc.returncode:raise ValueError('actual built-in symbols could not be read')
  rec['artifacts']['symbols']=evidence(nm);rec['source_after']=snapshot(root)
  if rec['source_after']!=before:raise ValueError('frozen runtime source changed during actual kernel build')
  rec['natural_exit']=0;rec['audit']=audit_kernel(rec)
  # The existing U20 native HOST test actually executes external network B.
  # Here prove the additional, narrower U23 fact: deploying that user package
  # changes neither the real built-in object nor the actual kernel ELF/input.
  model=root/'tests/nnrt/fixtures/external-model-b';staged=out/'external-model-b-user-package';staged.mkdir();model_rows=[]
  for p in sorted(model.iterdir()):
   if p.is_symlink() or not p.is_file() or p.stat().st_size>16*1024**2:raise ValueError('bounded regular frozen network B package files required')
   shutil.copyfile(str(p),str(staged/p.name));model_rows.append(evidence(staged/p.name))
  if not model_rows or not (staged/'manifest.json').is_file() or not (staged/'graph.json').is_file():raise ValueError('actual external model B package is incomplete')
  for key in ('vmlinux','driver'):
   if sha(rec['artifacts'][key]['path'])!=rec['artifacts'][key]['sha256']:raise ValueError('model B deployment changed actual kernel ELF')
  for row in kernel_inputs:
   if sha(ks/'drivers/misc/vxl'/row['path'])!=row['sha256']:raise ValueError('model B deployment changed actual driver/core inputs')
  rec['model_B_kernel_invariance']=dict(scope='ACTUAL_BUILTIN_ELF_AND_DRIVER_SOURCE_UNCHANGED_AFTER_USER_PACKAGE_STAGING_NOT_TARGET_EXECUTION',package=model_rows,vmlinux_sha256=rec['artifacts']['vmlinux']['sha256'],driver_sha256=rec['artifacts']['driver']['sha256'],kernel_source_id=rec['audit']['kernel_source_id'],target_executed=False,gpu_raw_cycles=None)
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

def repair_device_tree(env_path,provider_flag,original_path,after_path,source_lock_path,out):
 """Incremental DT-only repair in our inactive private provider project.

 The original failed outputs are archived by the caller and copied here
 before changing this project's DT recipe. Logs/receipts are new, and the
 real kernel source packet, Image and vmlinux must remain byte-identical.
 This is not a full-image build and does not lower its separate 15GiB gate.
 """
 import time
 from scripts.demo.image import resource_facts,monitored
 from scripts.unified.resources import resource_gate
 env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(env['root']).resolve();before=verify_source_lock(root,source_lock_path,env['source_lock_sha256'])
 for p,key in [(original_path,'kernel_original_receipt_sha256'),(after_path,'kernel_source_after_receipt_sha256')]:
  if sha(p)!=env[key]:raise ValueError('pinned original component/source-after receipt required')
 original=json.loads(pathlib.Path(original_path).read_text());after=json.loads(pathlib.Path(after_path).read_text());component_continuation(original,after)
 adapted=dict(natural_exit=0,target='rv32',target_executed=False,source_before=original['source_before'],source_after=after['snapshot'],commands=[],gpu_raw_cycles=None);require_cross_inputs(adapted,before,'rv32')
 declaration=provider(root,provider_flag);project=pathlib.Path(original['project']).resolve();parent=pathlib.Path(original['parent']).resolve()
 if original['provider']!=declaration or project not in pathlib.Path(original['kernel_build']).resolve().parents:raise ValueError('exact private provider project required')
 for c in original['commands']:
  if c['natural_exit']!=0 or sha(c['log'])!=c['log_sha256']:raise ValueError('original component command log differs')
 for row in original['artifacts'].values():
  if sha(row['path'])!=row['sha256']:raise ValueError('actual previously built kernel changed')
 dt=project/'images/linux/system.dtb'
 if sha(dt)!=env['kernel_original_dtb_sha256']:raise ValueError('exact first failed DT must be preserved before repair')
 resources=resource_gate(resource_facts(project),'cross');out=new_build_output(root,out);saved=out/'before-repair';saved.mkdir();backups=[];commands=[]
 rec=dict(scope='ACTUAL_PRIVATE_PROVIDER_DT_ONLY_REPAIR_KERNEL_BYTES_UNCHANGED_NOT_IMAGE_OR_BOARD',provider=declaration,source_before=before,project=str(project),original_receipt=dict(path=str(original_path),sha256=sha(original_path)),resources=resources,before=backups,commands=commands,gpu_raw_cycles=None)
 def row(p):return dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p))
 try:
  paths=[dt,project/'project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend']
  paths+=list((project/'project-spec/meta-user/recipes-bsp/device-tree/files').glob('*'))
  paths+=list((project/'components/plnx_workspace/device-tree/device-tree').glob('*.dts*'))
  for p in sorted(paths):
   if p.is_symlink() or not p.is_file() or p.stat().st_size>16*1024**2:raise ValueError('bounded actual original DT/configuration required')
   target=saved/p.relative_to(project);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(str(p),str(target));backups.append(dict(original=str(p),**row(target)))
  old_recipe=parent/'project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend';new_recipe=device_tree_recipe(old_recipe.read_bytes());recipe=project/'project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend';recipe.write_bytes(new_recipe)
  files=project/'project-spec/meta-user/recipes-bsp/device-tree/files';selected=root/'petalinux/unified'/('s8.dtsi' if provider_flag in (1,4) else 'p8.dtsi')
  # This repair restores the missing include chain, never changes DT contracts.
  if (files/'system-user.dtsi').read_bytes()!=selected.read_bytes():raise ValueError('frozen system-user DT contract differs')
  rec.update(parent_recipe=row(old_recipe),new_recipe=row(recipe));log=out/'actual-petalinux-device-tree-repair.log';argv=['bash','-c','set -e; . '+shlex.quote(env['petalinux_settings'])+'; petalinux-build -c device-tree'];start=time.monotonic()
  try:monitored(argv,project,log,project)
  finally:
   code=int(pathlib.Path(str(log)+'.exit').read_text()) if pathlib.Path(str(log)+'.exit').is_file() else None
   commands.append(dict(argv=argv,natural_exit=code,elapsed_s=time.monotonic()-start,log=str(log),log_sha256=sha(log) if log.is_file() else None))
  if re.search(r'(?m)^\s*(?:ERROR:|FATAL:|Traceback \()',log.read_text(errors='replace')):raise ValueError('actual DT command has failure semantics')
  for item in original['artifacts'].values():
   if sha(item['path'])!=item['sha256']:raise ValueError('DT-only repair changed actual kernel bytes')
  from scripts.unified.audit_kernel import audit_device_tree
  firmware=original['firmware'];rec['dtb']=row(dt);rec['audit']=audit_device_tree(dt.read_bytes(),declaration,firmware['catalog-v1.bin']['sha256'],firmware['execution-v1.bin']['sha256']);rec['source_after']=snapshot(root)
  if rec['source_after']!=before:raise ValueError('source changed during actual DT-only repair')
  rec.update(kernel_artifacts_unchanged=original['artifacts'],natural_exit=0)
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

def finish_kernel_components(env_path,provider_flag,original_path,after_path,source_lock_path,out):
 """Re-audit naturally successful components after the known layout mistake.

 Original failed receipt/logs are immutable. No compiler, configure, reset or
 hardware operation is repeated; only exact real built-in objects are linked
 into an audit artifact and the original kernel/DT are independently audited.
 """
 import copy,subprocess
 env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(env['root']).resolve();current=verify_source_lock(root,source_lock_path,env['source_lock_sha256'])
 for path,key in [(original_path,'kernel_original_receipt_sha256'),(after_path,'kernel_source_after_receipt_sha256')]:
  expected=env.get(key)
  if not isinstance(expected,str) or not re.fullmatch('[0-9a-f]{64}',expected) or sha(path)!=expected:raise ValueError('pinned original component and source-after receipts required')
 original=json.loads(pathlib.Path(original_path).read_text());after=json.loads(pathlib.Path(after_path).read_text());component_continuation(original,after)
 if after['original_receipt_sha256']!=sha(original_path) or original['provider']!=provider(root,provider_flag):raise ValueError('exact original provider/source-after pairing differs')
 for command in original['commands']:
  if sha(command['log'])!=command['log_sha256']:raise ValueError('original successful component log changed')
 for row in original['artifacts'].values():
  if sha(row['path'])!=row['sha256'] or pathlib.Path(row['path']).stat().st_size!=row['bytes']:raise ValueError('original actual vmlinux/Image changed after the first error')
 adapted=dict(natural_exit=0,target='rv32',target_executed=False,source_before=original['source_before'],source_after=after['snapshot'],commands=[],gpu_raw_cycles=None)
 require_cross_inputs(adapted,current,'rv32')
 out=new_build_output(root,out);rec=copy.deepcopy(original);rec.pop('error',None);rec['source_after']=after['snapshot'];rec['audit_preparation_source']=current;rec['original_failed_receipt']=dict(path=str(original_path),sha256=sha(original_path),natural_exit=1);rec['source_after_receipt']=dict(path=str(after_path),sha256=sha(after_path));rec['kernel_compile_repeated']=False;rec['artifacts']={}
 def evidence(path):
  p=pathlib.Path(path)
  if p.is_symlink() or not p.is_file() or not p.stat().st_size:raise ValueError('actual regular component artifact required')
  return dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p))
 try:
  kb=pathlib.Path(rec['kernel_build']);ks=pathlib.Path(rec['kernel_source']);project=pathlib.Path(rec['project']);bundle=bundle_builtin_driver(kb,env['aarch64_prefix'],out/'builtin-driver-evidence');rec['builtin_bundle']=bundle;rec['commands']+=bundle['commands']
  dtb=project/'images/linux/system.dtb'
  if env.get('dt_repair_receipt'):
   repair_path=pathlib.Path(env['dt_repair_receipt'])
   if sha(repair_path)!=env['dt_repair_receipt_sha256']:raise ValueError('pinned successful actual DT-only repair receipt required')
   repair=json.loads(repair_path.read_text())
   if repair.get('scope')!='ACTUAL_PRIVATE_PROVIDER_DT_ONLY_REPAIR_KERNEL_BYTES_UNCHANGED_NOT_IMAGE_OR_BOARD' or repair.get('natural_exit')!=0 or repair.get('source_before')!=repair.get('source_after') or repair.get('provider')!=rec['provider'] or repair.get('original_receipt',{}).get('sha256')!=sha(original_path) or repair.get('kernel_artifacts_unchanged')!=original['artifacts']:raise ValueError('actual matching DT repair and unchanged kernel evidence required')
   require_cross_inputs(dict(adapted,source_before=repair['source_before'],source_after=repair['source_after']),current,'rv32')
   if repair['dtb']!=evidence(dtb):raise ValueError('repaired actual DT bytes changed')
   rec['dt_repair_receipt']=dict(path=str(repair_path),sha256=sha(repair_path));rec['commands']+=repair['commands']
  for name,p in [('vmlinux',kb/'vmlinux'),('Image',kb/'arch/arm64/boot/Image'),('driver',pathlib.Path(bundle['artifact']['path'])),('config',kb/'.config'),('symvers',kb/'Module.symvers'),('dtb',dtb)]:rec['artifacts'][name]=evidence(p)
  symbols=out/'vmlinux.nm';argv=[env['aarch64_prefix']+'nm','--defined-only',str(kb/'vmlinux')]
  with symbols.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT)
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,log=str(symbols),log_sha256=sha(symbols)))
  if p.returncode:raise ValueError('actual complete kernel symbols unavailable')
  rec['artifacts']['symbols']=evidence(symbols);rec['natural_exit']=0
  from scripts.unified.audit_kernel import audit_kernel
  rec['audit']=audit_kernel(rec)
  staged=out/'external-model-b-user-package';staged.mkdir();rows=[];old={r['path']:r for r in original['source_before']['files']}
  for p in sorted((root/'tests/nnrt/fixtures/external-model-b').iterdir()):
   name=p.relative_to(root).as_posix()
   if p.is_symlink() or not p.is_file() or name not in old or sha(p)!=old[name]['sha256']:raise ValueError('original frozen external model B package differs')
   q=staged/p.name;shutil.copyfile(str(p),str(q));rows.append(evidence(q))
  if not rows or not (staged/'manifest.json').is_file() or not (staged/'graph.json').is_file():raise ValueError('complete original model B package required')
  for key in ('vmlinux','driver'):
   if sha(rec['artifacts'][key]['path'])!=rec['artifacts'][key]['sha256']:raise ValueError('model-only staging changed actual kernel/driver ELF')
  for row in rec['kernel_input_files']:
   if sha(ks/'drivers/misc/vxl'/row['path'])!=row['sha256']:raise ValueError('model-only staging changed actual driver/core input')
  rec['model_B_kernel_invariance']=dict(scope='ACTUAL_BUILTIN_ELF_AND_DRIVER_SOURCE_UNCHANGED_AFTER_USER_PACKAGE_STAGING_NOT_TARGET_EXECUTION',package=rows,vmlinux_sha256=rec['artifacts']['vmlinux']['sha256'],driver_sha256=rec['artifacts']['driver']['sha256'],kernel_source_id=rec['audit']['kernel_source_id'],target_executed=False,gpu_raw_cycles=None)
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

def builtin_makefile(raw):
 if not isinstance(raw,bytes):raise ValueError('actual driver Makefile bytes required')
 for old,new in [(b'obj-m += vxl_linux.o',b'obj-$(CONFIG_VXL_LINUX) += vxl_linux.o'),(b'-I$(src)/../include -Werror',b'-I$(srctree)/drivers/misc/vxl/include -Werror')]:
  if raw.count(old)!=1:raise ValueError('unambiguous original built-in conversion required')
  raw=raw.replace(old,new)
 return raw

def identity_dtsi(declaration,catalog_sha256,execution_sha256):
 flag=declaration.get('provider_flags')
 validate_provider_identity(declaration)
 values={'hardware-contract':declaration['hardware_contract'],'bit':declaration['bit_sha256'],'xsa':declaration['xsa_sha256'],'catalog':catalog_sha256,'execution':execution_sha256}
 lines=['/* Declared boot identity; not an independent physical hardware hash. */','&{/amba_pl@0/vortex-backend@a0000000} {','    compatible = "'+declaration['compatible']+'";','    vortex,compute-v2;']
 for name,value in values.items():
  if not isinstance(value,str) or not re.fullmatch('[0-9a-f]{64}',value):raise ValueError('exact digest required')
  lines.append('    vortex,'+name+'-sha256 = ['+' '.join(value[i:i+2] for i in range(0,64,2))+'];')
 return '\n'.join(lines+['};',''])

def catalog_entries(raw):
 if not isinstance(raw,bytes) or not 64<=len(raw)<=65536:raise ValueError('bounded original catalog required')
 magic,version,size,count=struct.unpack_from('<4I',raw)
 if (magic,version,size)!=(0x434c5856,1,len(raw)) or not 1<=count<=16 or len(raw)<64+512*count or raw[16:48]!=hashlib.sha256(raw[64:]).digest():raise ValueError('original catalog header/digest differs')
 rows=[]
 for i in range(count):
  start=64+512*i;fields=list(struct.unpack_from('<16I',raw,start));digests=[raw[start+64+32*j:start+96+32*j] for j in range(8)]
  if fields[11]<64+512*count or fields[11]+fields[12]>len(raw):raise ValueError('catalog schema outside original file')
  schema=raw[fields[11]:fields[11]+fields[12]];rows.append(dict(fields=fields,digests=digests,schema=schema))
 if build_catalog(rows)!=raw:raise ValueError('original catalog is not canonical or contains unaudited bytes')
 return rows

def _nn_entry(root,name,logical,declaration):
 folder=root/'data/nnrt/programs'/name;family=json.loads((folder/'family.json').read_text());runtime=json.loads((folder/'rv32-build.json').read_text())['runtime'];code=(folder/'code.bin').read_bytes();schema=(folder/'args-schema.bin').read_bytes();ro=(folder/'rodata.bin').read_bytes() if name.startswith('mosaic-int16-') else b''
 digest=lambda raw:hashlib.sha256(raw).digest()
 if len(code)!=65536 or digest(code).hex()!=family['code_sha256'] or digest(schema).hex()!=family['schema_sha256'] or (ro and digest(ro).hex()!=family['rodata_sha256']):raise ValueError('frozen NN source/program/schema changed')
 header=struct.unpack_from('<16I',schema)
 if (header[3],header[5],header[7],header[8],header[9],header[10],header[11])!=(128,0x80000000,0x10002000,4096,0xfff00000,1048576,1):raise ValueError('frozen NN schema address domains differ')
 expected=dict(entry=0x80000000,args_gpu=0x80100040,args_bytes=128,crt_gpu=0x10002000,crt_bytes=4096,retire_offset=8,retire_stride=256,retire_value=0,retire_count=4,mutable_state_domain='PS_CRT',stack_gpu=0xfff00000,stack_bytes=1048576,stack_stride=8192,fcsr_init=0)
 if any(runtime.get(k)!=v for k,v in expected.items()) or (header[6]!=(0x80110000 if ro else 0)):raise ValueError('frozen CRT/retirement/readonly layout differs')
 original_guards=[dict(gpu=0x80000000,offset=0xffc0,bytes=64,stride=64,count=1),dict(gpu=0x10002000,offset=0xfc0,bytes=64,stride=64,count=1),dict(gpu=0xfff00000,offset=0x70000,bytes=64,stride=8192,count=64)]
 if runtime.get('guards')!=original_guards:raise ValueError('original CODE/CRT/per-hart stack guards changed')
 fields=[1,1,logical,0,0,len(code),len(code),len(ro),runtime['entry'],runtime['args_gpu'],128,0,0,0,1,0]
 # Keep the actual frozen CODE digest as the NN program's source identity;
 # runtime source manifests stay separate from immutable shader identities.
 digests=[digest(code),digest(ro),digest(schema)]+[bytes.fromhex(declaration[k]) for k in ('hardware_contract','bit_sha256','xsa_sha256')]+[bytes(32),digest(code)]
 return dict(fields=fields,digests=digests,schema=schema,payload=code+ro,runtime=runtime,name=name)

def firmware_inputs(root,provider_flag,*,mosaic_variant=None,mosaic_fused=None):
 """Build isolated boot files from frozen bytes, never from HOST test bridges."""
 from scripts.unified.mosaic_program import resolve_variant
 mosaic_variant=resolve_variant(mosaic_variant,fused=mosaic_fused)
 root=pathlib.Path(root);declaration=provider(root,provider_flag)
 if provider_flag==4:return joint_firmware_inputs(root,declaration,mosaic_variant=mosaic_variant)
 if provider_flag==2:
  folder=root/'data/p8/programs';catalog=(folder/'catalog.bin').read_bytes();execution=(folder/'execution-v2.bin').read_bytes();rows=catalog_entries(catalog)
  if [(r['fields'][0],r['fields'][1],r['fields'][2]) for r in rows]!=[(2,5,i) for i in (9,10,11,12)]:raise ValueError('original P8 programs9..12 required')
  for r in rows:r['payload']=(folder/(r['digests'][6].hex()+'.bin')).read_bytes()
  if len(execution)!=1088 or struct.unpack_from('<4I',execution)!=(0x454c5856,2,1088,4) or execution[16:48]!=hashlib.sha256(catalog).digest():raise ValueError('original P8 execution policy binding differs')
 else:
  from scripts.unified.mosaic_program import selected_name
  from scripts.unified.hunet_program import selected_name as selected_hunet
  rows=[_nn_entry(root,n,p,declaration) for n,p in [('int16-family-v1',65537),(selected_hunet(root),65552),(selected_name(root,variant=mosaic_variant),65568)]]
  folder=root/'data/image/programs-v2';image=catalog_entries((folder/'catalog.bin').read_bytes())
  if [(r['fields'][0],r['fields'][1],r['fields'][2]) for r in image]!=[(3,256,512+i) for i in range(10)]:raise ValueError('original IMAGE operations0..9 required')
  for r in image:r['payload']=(folder/(r['digests'][6].hex()+'.bin')).read_bytes()
  rows+=image;catalog=build_catalog(rows);policies=[];bound=catalog_entries(catalog)
  for original,new in zip(rows,bound):
   if original['fields'][0]==1:
    # Code object's own tail guard is enforced by the program loader. Preserve
    # the actual CRT +64 per-hart guards, and add a separate stack-base sentinel
    # required by the common execution binder. Do not replace either guard.
    guards=[[0x10002000,0xfc0,64,64,1],[0xfff00000,0,64,64,1],[0xfff00000,0x70000,64,8192,64]]
    values=[WATCHDOG,ABSENT,8,256,0,4]+[0]*6+[ABSENT,ABSENT,0,0,0,0]+[3]+sum(guards,[])+[0]*5+[0]*17
   else:
    # The original image shader has no CRT. Use the existing explicit global
    # drain mode, with no fabricated exit words, stores or changed shader code.
    values=[WATCHDOG,ABSENT,0,0,0,0]+[0]*6+[ABSENT,ABSENT,0,0,0,0]+[0]+[0]*20+[1]+[0]*16
   if len(values)!=56:raise ValueError('exact trusted execution policy layout required')
   policies.append(new['digests'][6]+struct.pack('<56I',*values))
  execution=struct.pack('<4I',0x454c5856,2,64+256*len(rows),len(rows))+hashlib.sha256(catalog).digest()+bytes(16)+b''.join(policies)
 bound=catalog_entries(catalog);entries=[]
 identity=[bytes.fromhex(declaration[k]) for k in ('hardware_contract','bit_sha256','xsa_sha256')]
 for original,row in zip(rows,bound):
  f=row['fields'];payload=original['payload']
  if row['digests'][3:6]!=identity or f[14]!=provider_flag or len(payload)!=max(f[4]+f[5],f[6]+f[7]) or hashlib.sha256(payload[f[4]:f[4]+f[5]]).digest()!=row['digests'][0] or hashlib.sha256(payload[f[6]:f[6]+f[7]]).digest()!=row['digests'][1]:raise ValueError('actual provider/CODE/RODATA/container identity differs')
  entries.append(dict(namespace=f[0],logical_profile=f[1],logical_program=f[2],entry_id=row['digests'][6].hex(),payload=payload,schema=row['schema']))
 return dict(scope='PRODUCTION_BOOT_FIRMWARE_INPUTS_NOT_GPU_EXECUTED',provider=declaration,catalog=catalog,execution=execution,entries=entries,watchdog=WATCHDOG,gpu_raw_cycles=None)

def joint_firmware_inputs(root,declaration,*,mosaic_variant=None,mosaic_fused=None):
 """Rebind immutable original programs to one explicitly versioned S8 platform.

 Schema bytes retain their original provider (NN/IMAGE=1, graphics=2).
 The C catalog verifier checks that origin against the joint namespace.
 Execution policies are copied from the validated original provider output;
 only their catalog entry IDs and catalog digest change.
 """
 from scripts.unified.mosaic_program import resolve_variant
 mosaic_variant=resolve_variant(mosaic_variant,fused=mosaic_fused)
 rows=[];original=[]
 identity=[bytes.fromhex(declaration[k]) for k in ('hardware_contract','bit_sha256','xsa_sha256')]
 for flag in (1,2):
  frozen=firmware_inputs(root,flag,mosaic_variant=mosaic_variant)
  for i,(row,entry) in enumerate(zip(catalog_entries(frozen['catalog']),frozen['entries'])):
   if flag==1 and row['fields'][0]==1 and row['fields'][2]==65537:continue
   policy=frozen['execution'][64+256*i:64+256*(i+1)]
   if len(policy)!=256 or policy[:32]!=row['digests'][6]:raise ValueError('original execution policy entry binding differs')
   row['fields'][14]=4;row['digests'][3:6]=identity
   rows.append(row);original.append((entry,policy[32:]))
 if len(rows)!=16:raise ValueError('joint release must contain exactly2 NN +10 IMAGE +4 P8 programs')
 catalog=build_catalog(rows);bound=catalog_entries(catalog);entries=[];policies=[]
 for row,(old,policy) in zip(bound,original):
  f=row['fields'];payload=old['payload']
  if hashlib.sha256(row['schema']).digest()!=row['digests'][2] or hashlib.sha256(payload[f[4]:f[4]+f[5]]).digest()!=row['digests'][0] or hashlib.sha256(payload[f[6]:f[6]+f[7]]).digest()!=row['digests'][1]:raise ValueError('joint binding changed original shader/schema data')
  entries.append(dict(namespace=f[0],logical_profile=f[1],logical_program=f[2],entry_id=row['digests'][6].hex(),payload=payload,schema=row['schema']))
  policies.append(row['digests'][6]+policy)
 execution=struct.pack('<4I',0x454c5856,2,64+256*len(rows),len(rows))+hashlib.sha256(catalog).digest()+bytes(16)+b''.join(policies)
 return dict(scope='PRODUCTION_BOOT_FIRMWARE_INPUTS_NOT_GPU_EXECUTED',provider=declaration,catalog=catalog,execution=execution,entries=entries,watchdog=WATCHDOG,gpu_raw_cycles=None)

def verify_mosaic_firmware_binding(root,firmware,model_directory,*,mosaic_variant=None):
 from scripts.unified.mosaic_program import selected_directory
 family_dir=selected_directory(pathlib.Path(root),variant=mosaic_variant);family=json.loads((family_dir/'family.json').read_bytes())
 model_directory=pathlib.Path(model_directory);binding=json.loads((model_directory/'binding.json').read_bytes());requirements=json.loads((model_directory/'program-requirements.json').read_bytes())
 expected=(family['code_sha256'],family['rodata_sha256'],family['schema_sha256'])
 if tuple(binding.get(k) for k in ('code_sha256','readonly_sha256','schema_sha256'))!=expected:raise ValueError('model differs from explicitly selected Mosaic family')
 programs=requirements['programs']
 if len(programs)!=1 or tuple(programs[0].get(k) for k in ('namespace','profile','program','code','schema'))!=(1,1,65568,expected[0],expected[2]):raise ValueError('model Mosaic requirement differs')
 rows=[r for r in catalog_entries(firmware['catalog']) if r['fields'][:3]==[1,1,65568]]
 entries=[e for e in firmware['entries'] if (e['namespace'],e['logical_profile'],e['logical_program'])==(1,1,65568)]
 if len(rows)!=1 or len(entries)!=1:raise ValueError('exactly one actual Mosaic catalog entry required')
 row=rows[0];entry=entries[0];code=(family_dir/'code.bin').read_bytes();ro=(family_dir/'rodata.bin').read_bytes();schema=(family_dir/'args-schema.bin').read_bytes()
 if [d.hex() for d in row['digests'][:3]]!=list(expected) or row['schema']!=schema or entry['schema']!=schema or entry['payload']!=code+ro:raise ValueError('actual Mosaic catalog CODE/RO/schema differs from model family')
 return dict(variant=mosaic_variant or 'pack32-ro64-v3',code_sha256=expected[0],readonly_sha256=expected[1],schema_sha256=expected[2],catalog_entry_id=entry['entry_id'])

if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser();p.add_argument('--env',required=True);p.add_argument('--provider',required=True,type=int,choices=(1,2,4));p.add_argument('--source-lock',required=True);p.add_argument('--out',required=True);p.add_argument('--finish-existing');p.add_argument('--source-after');p.add_argument('--repair-dt-existing');p.add_argument('--mosaic-variant');a=p.parse_args()
 if a.mosaic_variant and (a.repair_dt_existing or a.finish_existing):p.error('--mosaic-variant applies to a new build; continuation must retain its receipt identity')
 if a.repair_dt_existing:
  if a.finish_existing or not a.source_after:p.error('DT repair requires source-after and cannot also finish-existing')
  result=repair_device_tree(a.env,a.provider,a.repair_dt_existing,a.source_after,a.source_lock,a.out)
 elif a.finish_existing:
  if not a.source_after:p.error('--source-after is required for an independent component continuation audit')
  result=finish_kernel_components(a.env,a.provider,a.finish_existing,a.source_after,a.source_lock,a.out)
 else:result=build_kernel(a.env,a.provider,a.source_lock,a.out,mosaic_variant=a.mosaic_variant)
 print(json.dumps(dict(scope=result['scope'],natural_exit=result['natural_exit'],provider_flags=a.provider,kernel_source_id=result['audit'].get('kernel_source_id'),board_identity_verified=False,gpu_raw_cycles=None)))
