"""Isolated S8 image workflow. No board, reset, JTAG or media-write command."""
from pathlib import Path
import argparse,datetime,gzip,hashlib,io,json,os,re,shlex,shutil,socket,subprocess,sys,tarfile
from contextlib import contextmanager
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'hunet'))
from scripts.hunet import image_audit as legacy
from scripts.hunet import prepare_image as parent
from scripts.s8.package_release import verify_release,sha,js
from scripts.s8.build_rv32 import run
ROOT=Path(__file__).resolve().parents[2]
CONFIG=('CONFIG_VXL_LINUX=y','CONFIG_ZONE_DMA32=y','CONFIG_DEBUG_FS=y','CONFIG_CRYPTO=y','CONFIG_CRYPTO_HASH=y','CONFIG_CRYPTO_SHA256=y')
def content_errors(actual,expected):return ['content:'+k for k,v in expected.items() if actual.get(k)!=v]
def dt_errors(props):
 # Reuse the tested resource/phandle/Linux-memory validator; only this
 # candidate's compatible differs. An old compatible is rejected explicitly.
 old=dict(props);old['compatible']='vortex,hw4-hunet-v5';errors=legacy.dt_errors(old)
 if props.get('compatible')!='vortex,hw4-s8-validation-v1':errors.append('S8 compatible')
 return errors
def resource_ok(facts):
 return facts.get('host')=='z2euss-virtual-machine' and facts.get('uid')==1000 and facts.get('heavy')==[] and facts.get('coordination') is True and facts.get('memory',0)>=4*1024**3 and facts.get('disk',0)>=15*1024**3
def lock_config(env):
 if env.get('coordination_owner')!='S8' or not env.get('coordination_file'):raise ValueError('explicit S8 coordination required')
 return Path(env['coordination_file']).parent/'S8-IMAGE-OWNER.lock'
@contextmanager
def image_lock(env):
 import fcntl
 config=json.loads(Path(env).read_text());lock=lock_config(config)
 if not Path(config['coordination_file']).is_file():raise ValueError('coordination document missing')
 with lock.open('a+') as handle:
  try:fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:raise ValueError('RESOURCE_REFUSED: image lock already held')
  marker=Path(config['coordination_file'])
  with marker.open('a') as stream:stream.write('\nUTC='+datetime.datetime.now(datetime.timezone.utc).isoformat()+' owner=S8 image lock acquired; jobs1 RAM4GiB disk15GiB; no C process/board/SD action.\n')
  try:yield
  finally:
   with marker.open('a') as stream:stream.write('UTC='+datetime.datetime.now(datetime.timezone.utc).isoformat()+' owner=S8 image lock released.\n')
def _resource(path,env):
 values={s.split(':')[0]:int(s.split()[1])*1024 for s in Path('/proc/meminfo').read_text().splitlines() if s.split()[1].isdigit()}
 processes=subprocess.check_output(['ps','-eo','comm']).decode().splitlines();heavy=sorted(set(processes)&set(('make','cc1','cc1plus','bitbake','vivado','ninja','verilator_bin')))
 marker=Path(env['coordination_file']);facts=dict(host=socket.gethostname(),uid=os.getuid(),heavy=heavy,coordination=marker.is_file() and env.get('coordination_owner')=='S8',memory=values['MemAvailable'],disk=shutil.disk_usage(path).free,captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),jobs=1)
 if not resource_ok(facts):raise ValueError('RESOURCE_REFUSED:'+json.dumps(facts))
 return facts
def preflight(project,env):
 project=Path(project);out=project.parent/(project.name+'-resources.json')
 if out.exists():raise ValueError('fresh resource receipt required')
 try:
  with image_lock(env):facts=_resource(project.parent,json.loads(Path(env).read_text()))
  record=dict(state='RESOURCE_ADMITTED_NOT_BUILD',exit=0,resources=facts)
 except ValueError as exc:record=dict(state='RESOURCE_REFUSED',exit=2,error=str(exc))
 record.update(scope='ACTUAL_UBUNTU_IMAGE_RESOURCE_CHECK_NOT_BUILD',build_calls=0,board='NOT_RUN',gpu_raw_cycles=None);js(out,record);return record
def _wait(release,project):
 receipt=Path(release)/'receipt.json'
 if receipt.is_file():
  value=json.loads(receipt.read_text())
  if value.get('state')=='WAIT_HW_EXPORT':
   value=dict(value,image='NOT_BUILT',board='NOT_RUN',gpu_raw_cycles=None)
   target=Path(project).parent/(Path(project).name+'-wait.json')
   if target.exists():raise ValueError('fresh WAIT receipt path required')
   js(target,value);return value
 return None
def _kernel_make(text):
 return re.sub(r'^ccflags-y \+= -I.* -Werror$', 'ccflags-y += -I$(srctree)/drivers/misc/vxl/include -Werror',text.replace('obj-m += vxl_linux.o','obj-$(CONFIG_VXL_LINUX) += vxl_linux.o'),flags=re.M)
def cache_sources(old):
 old=Path(old).resolve();record=old.parent/'integration.json'
 if not record.is_file():raise ValueError('owned parent cache receipt missing')
 value=json.loads(record.read_text())
 if not value.get('scope','').startswith('HW4_HUNET_') or value.get('manifest',{}).get('profile')!=4:raise ValueError('unrelated parent cache owner')
 result=[old/'build'/name for name in ('downloads','sstate-cache')]
 for path in result:
  actual=path.resolve()
  if not path.is_dir() or not any(p.parent==old.parent.parent and p.name.startswith('codex_linux_backend_hunet_v5_') for p in actual.parents):raise ValueError('offline parent cache missing/escaped')
 return result
def _legacy_template_reference():
 r=parent.recipes('1'*40,'1'*32)
 kernel=r['kernel'].replace('vxl_hunet_source.h','vxl_s8_release.h')
 apps='''SUMMARY = "Exact controlled S8 userspace and data"
LICENSE = "CLOSED"
SRC_URI = "file://s8-release.tar.gz;subdir=s8-release"
S = "${WORKDIR}/s8-release"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
do_install() {
    install -d ${D}${bindir} ${D}${libdir} ${D}${datadir}/codex-s8
    install -m 0755 vxl_s8 ${D}${bindir}/
    install -m 0755 libvxl-s8.so ${D}${libdir}/
    cp -R --no-preserve=ownership ${S}/. ${D}${datadir}/codex-s8/
}
PACKAGES =+ "${PN}-gpu"
FILES_${PN}-gpu = "${datadir}/codex-s8/kernel.elf"
INSANE_SKIP_${PN}-gpu = "arch"
RDEPENDS_${PN} += "${PN}-gpu"
FILES_${PN} += "${libdir}/libvxl-s8.so ${datadir}/codex-s8"
FILES_${PN}-dev = ""
INSANE_SKIP_${PN} += "dev-so ldflags already-stripped"
'''
 return dict(kernel=kernel,apps=apps,firstboot=r['firstboot'].replace('codex-hunet-manifest','codex-s8-manifest'),dt=parent.dt_source().replace('vortex,hw4-hunet-v5','vortex,hw4-s8-validation-v1'))
def recipes():
 names=dict(kernel='linux-xlnx_%.bbappend',apps='vxl-runtime.bb',firstboot='codex-firstboot.bb',dt='system-user.dtsi')
 return {k:(ROOT/'petalinux/s8'/v).read_text() for k,v in names.items()}
def _shell(settings,args,cwd,log):
 command='set -e; . '+shlex.quote(str(settings))+'; '+shlex.join([str(x) for x in args]) if hasattr(shlex,'join') else 'set -e; . '+shlex.quote(str(settings))+'; '+' '.join(shlex.quote(str(x)) for x in args)
 run(['bash','-c',command],log) if cwd is None else _run_cwd(['bash','-c',command],cwd,log)
def _run_cwd(command,cwd,log):
 with Path(log).open('xb') as f:p=subprocess.run(command,cwd=str(cwd),stdout=f,stderr=subprocess.STDOUT)
 Path(str(log)+'.exit').write_text(str(p.returncode)+'\n')
 if p.returncode:raise ValueError('command exit '+str(p.returncode)+' '+str(log))
def prepare(release,project,env):
 wait=_wait(release,project)
 if wait:return wait
 with image_lock(env):return _prepare(release,project,env)
def _prepare(release,project,env):
 release=Path(release).resolve();project=Path(project).resolve();wait=_wait(release,project)
 if wait:return wait
 manifest=verify_release(release);env=json.loads(Path(env).read_text());old=Path(env['parent_project']).resolve()
 if project.exists() or old==project or old in project.parents or project in old.parents:raise ValueError('fresh disjoint project required')
 if not (old/'project-spec/configs/config').is_file():raise ValueError('matching parent configuration required')
 caches=cache_sources(old);gate=_resource(project.parent,env);project.mkdir();meta=project/'project-spec/meta-user'
 # Copy only project scaffolding and configuration; no old bit, images,
 # executable recipes or build output can become a candidate artifact.
 for item in old.iterdir():
  if item.name in ('build','images','project-spec','.git') or item.name.endswith('.log'):continue
  if item.is_file():shutil.copy2(str(item),str(project/item.name))
  elif item.is_dir() and item.name=='.petalinux':shutil.copytree(str(item),str(project/item.name))
 shutil.copytree(str(old/'project-spec/configs'),str(project/'project-spec/configs'))
 (project/'build').mkdir()
 for cache in caches:(project/'build'/cache.name).symlink_to(cache.resolve(),target_is_directory=True)
 (meta/'conf').mkdir(parents=True)
 for name in ('layer.conf','petalinuxbsp.conf'):
  src=old/'project-spec/meta-user/conf'/name
  if src.is_file():shutil.copy2(str(src),str(meta/'conf'/name))
 packages=old/'project-spec/meta-user/conf/user-rootfsconfig'
 if not packages.is_file():raise ValueError('matching parent user-rootfsconfig required')
 symbols=packages.read_text().splitlines()
 for symbol in ('CONFIG_vxl-runtime','CONFIG_codex-firstboot'):
  if symbol not in symbols:symbols.append(symbol)
 (meta/'conf/user-rootfsconfig').write_text('\n'.join(symbols)+'\n')
 r=recipes()
 def write(name,data):
  p=meta/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data.encode() if isinstance(data,str) else data)
 write('recipes-kernel/linux/linux-xlnx_%.bbappend',r['kernel']);write('recipes-kernel/linux/files/vxl-built-in.cfg','\n'.join(CONFIG)+'\n')
 write('recipes-kernel/linux/files/vxl_s8_release.h',(release/'vxl_s8_release.h').read_bytes());write('recipes-kernel/linux/files/vxl-source.tar.gz',(release/'source.tar.gz').read_bytes())
 write('recipes-apps/vxl-runtime/vxl-runtime.bb',r['apps']);dest=meta/'recipes-apps/vxl-runtime/files/s8-release.tar.gz';dest.parent.mkdir(parents=True,exist_ok=True)
 with tarfile.open(str(dest),'w:gz') as tar:
  for name in sorted(set(manifest['files'])|{'manifest.json'}):tar.add(str(release/name),arcname=name,recursive=False)
 public=Path(env.get('authorized_key_file',str(old/'project-spec/meta-user/recipes-apps/codex-firstboot/files/authorized_keys')))
 if not public.is_file() or not public.read_bytes().startswith((b'ssh-rsa ',b'ssh-ed25519 ')):raise ValueError('trusted public key required')
 build_id='S8-LINUX-VALIDATION-V1-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
 identity=dict(build_id=build_id,source_sha256=manifest['source_sha256'],binding_sha256=manifest['binding_sha256'],profile=5,program=11,bit_sha256=manifest['digests']['bit'],xsa_sha256=manifest['digests']['xsa'])
 write('recipes-apps/codex-firstboot/codex-firstboot.bb',r['firstboot']);write('recipes-apps/codex-firstboot/files/authorized_keys',public.read_bytes());write('recipes-apps/codex-firstboot/files/codex-build-id',build_id+'\n');write('recipes-apps/codex-firstboot/files/codex-s8-manifest.json',json.dumps(identity,sort_keys=True)+'\n')
 write('recipes-apps/codex-firstboot/files/codex-direct-network','#!/bin/sh\ncase "$1" in start) ip link set dev eth0 up && ip addr replace 192.168.13.10/24 dev eth0;; esac\n')
 write('recipes-bsp/device-tree/files/system-user.dtsi',r['dt'])
 for name in ('boot.cmd.default','boot.cmd.default.initrd'):write('recipes-bsp/u-boot/u-boot-zynq-scr/'+name,parent.BOOT_COMMAND)
 rootfs=project/'project-spec/configs/rootfs_config';lines=rootfs.read_text().splitlines();lines=[s for s in lines if not re.search(r'^CONFIG_(?:vxl-|codex-)',s)];rootfs.write_text('\n'.join(lines)+'\nCONFIG_vxl-runtime=y\nCONFIG_codex-firstboot=y\n')
 with (meta/'conf/petalinuxbsp.conf').open('a') as f:f.write(parent.offline_settings().replace('"2"','"1"').replace('-j 2','-j 1'))
 hw=project/'s8-hardware';hw.mkdir();shutil.copyfile(str(release/'hardware/system.xsa'),str(hw/'system.xsa'))
 # PetaLinux2020.1 assumes this destination exists before pushd/cleanup.
 # A failed pushd would otherwise leave cleanup in the project directory.
 (project/'project-spec/hw-description').mkdir()
 _shell(env['petalinux_settings'],['petalinux-config','--get-hw-description='+str(hw),'--silentconfig'],project,project.parent/(project.name+'-import-hardware.log'))
 integration=dict(scope='S8_ISOLATED_IMAGE_PREPARATION_NOT_IMAGE_PASS',release=str(release),project=str(project),identity=identity,resources=gate,cache_mode='OWN_HW4_SHARED_SERIALIZED',cache_sources=[str(p.resolve()) for p in caches],public_key_sha256=sha(public),kernel_makefile=_kernel_make((ROOT/'driver/Makefile').read_text()),state='PREPARED_NOT_BUILT',exit=0,board='NOT_RUN',gpu_raw_cycles=None)
 js(project/'s8-integration.json',integration);return integration
def build(release,project,env):
 wait=_wait(release,project)
 if wait:return wait
 with image_lock(env):return _build(release,project,env)
def _build(release,project,env):
 release=Path(release).resolve();project=Path(project).resolve();wait=_wait(release,project)
 if wait:return wait
 manifest=verify_release(release);configuration=json.loads(Path(env).read_text());gate=_resource(project.parent,configuration)
 if not (project/'s8-integration.json').is_file():_prepare(release,project,env)
 identity=json.loads((project/'s8-integration.json').read_text())
 if identity['identity']['binding_sha256']!=manifest['binding_sha256']:raise ValueError('prepared binding drift')
 _shell(configuration['petalinux_settings'],['petalinux-build'],project,project/'s8-build.log')
 image=project/'images/linux';args=['petalinux-package','--boot','--fsbl',str(image/'zynqmp_fsbl.elf'),'--fpga',str(release/'hardware/top.bit'),'--u-boot',str(image/'u-boot.elf'),'--pmufw',str(image/'pmufw.elf'),'--atf',str(image/'bl31.elf'),'--force','--output',str(image/'BOOT.BIN')]
 components={name:sha(image/name) for name in ('zynqmp_fsbl.elf','u-boot.elf','pmufw.elf','bl31.elf')};_shell(configuration['petalinux_settings'],args,project,project/'s8-boot-package.log')
 receipt=dict(state='BUILT_PENDING_CONTENT_AUDIT',exit=0,scope='S8_ACTUAL_IMAGE_BUILD_NOT_BOARD',resources=gate,components=components,board='NOT_RUN',gpu_raw_cycles=None);js(project/'s8-build-receipt.json',receipt);return receipt
def audit(release,artifacts):
 release=Path(release).resolve();project=Path(artifacts).resolve();manifest=verify_release(release);integration=json.loads((project/'s8-integration.json').read_text());image=project/'images/linux';out=project/'s8-image-audit';out.mkdir(exist_ok=False);errors=[]
 result=dict(scope='ACTUAL_S8_IMAGE_CONTENT_NOT_BOARD',integrity='FAIL',gpu_raw_cycles=None)
 try:
  builds=list((project/'build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx').glob('*-r0/linux-*-build'))
  if len(builds)!=1:raise ValueError('ambiguous actual kernel')
  kernel=builds[0];native=kernel.parent/'recipe-sysroot-native/usr/bin';fdtget=str(native/'fdtget');dumpimage=str(native/'dumpimage')
  errors+=dt_errors(legacy.dt_properties(image/'system.dtb',fdtget))
  for i,name in enumerate(('fit-kernel.bin','fit-dt.bin','fit-rootfs.bin')):subprocess.check_call([dumpimage,'-T','flat_dt','-p',str(i),'-o',str(out/name),str(image/'image.ub')],stdout=subprocess.DEVNULL)
  if gzip.decompress((out/'fit-kernel.bin').read_bytes())!=(image/'Image').read_bytes() or (out/'fit-dt.bin').read_bytes()!=(image/'system.dtb').read_bytes() or (out/'fit-rootfs.bin').read_bytes()!=(image/'rootfs.cpio.gz').read_bytes():errors.append('FIT actual payloads')
  for node,name in (('kernel@1','fit-kernel.bin'),('fdt@system-top.dtb','fit-dt.bin'),('ramdisk@1','fit-rootfs.bin')):
   base='/images/'+node;children=subprocess.check_output([fdtget,'-l',str(image/'image.ub'),base]).decode().split();checks=0
   for child in children:
    path=base+'/'+child
    if subprocess.check_output([fdtget,'-t','s',str(image/'image.ub'),path,'algo']).decode().strip()=='sha256':
     actual=bytes(int(v,16) for v in subprocess.check_output([fdtget,'-t','bx',str(image/'image.ub'),path,'value']).decode().split()).hex();checks+=1
     if actual!=sha(out/name):errors.append('FIT SHA '+node)
   if checks!=1:errors.append('FIT SHA coverage '+node)
  parser=legacy.legacy_cpio();cpio=gzip.decompress((out/'fit-rootfs.bin').read_bytes());recipe=project/'components/yocto/layers/core/meta/classes/image_types.bbclass';recipe_sha=sha(recipe)
  errors+=parser.rootfs_payload_errors(cpio,image/'rootfs.tar.gz',recipe_sha)
  if recipe_sha!=parser.CPIO_INIT_RECIPE_SHA256:errors.append('unrecognized actual init recipe')
  entries=parser.cpio_entries(cpio);files=legacy.resolved_regular_files(entries);prefix='usr/share/codex-s8/'
  for name,row in manifest['files'].items():
   raw=files.get(prefix+name,b'')
   if len(raw)!=row['bytes'] or hashlib.sha256(raw).hexdigest()!=row['sha256']:errors.append('rootfs payload '+name)
  if files.get(prefix+'manifest.json')!=(release/'manifest.json').read_bytes():errors.append('rootfs manifest')
  for installed,payload in (('usr/bin/vxl_s8','vxl_s8'),('usr/lib/libvxl-s8.so','libvxl-s8.so')):
   if files.get(installed)!=(release/payload).read_bytes():errors.append('installed actual userspace '+installed)
  if files.get('etc/codex-build-id')!=(integration['identity']['build_id']+'\n').encode():errors.append('build identity')
  key=files.get('home/root/.ssh/authorized_keys',b'')
  if hashlib.sha256(key).hexdigest()!=integration['public_key_sha256']:errors.append('trusted key')
  errors+=legacy.ssh_entry_errors(entries)
  config=(kernel/'.config').read_text().splitlines();symbols=(kernel/'System.map').read_text();binding=(release/'binding.bin').read_bytes()
  if any(v not in config for v in CONFIG):errors.append('actual built-in/debugfs/crypto config')
  if sha(kernel/'.config')!=manifest['digests']['kernel_config']:errors.append('target config changed: rebind required')
  for n in ('vxl_s8_attach','vxl_s8_prepare','vxl_s8_tracefs_init','vxl_s8_complete','poll_gpu'):
   if not re.search(r'^\S+\s+[Tt]\s+'+n+'$',symbols,re.M):errors.append('actual builtin '+n)
  if binding not in (image/'Image').read_bytes() or binding not in (image/'vmlinux').read_bytes():errors.append('actual compiled full binding')
  actual=project/'build/tmp/work-shared/zynqmp-generic/kernel-source/drivers/misc/vxl';frozen=json.loads((release/'source-freeze.json').read_text())
  for row in frozen['source_files']:
   name=row['path']
   if name.split('/')[0] not in ('core','driver','include','generated') or name=='driver/Makefile':continue
   if not (actual/name).is_file() or sha(actual/name)!=row['sha256']:errors.append('actual kernel source '+name)
  if (actual/'driver/Makefile').read_text()!=integration['kernel_makefile'] or (actual/'driver/vxl_s8_release.h').read_bytes()!=(release/'vxl_s8_release.h').read_bytes():errors.append('actual integration/binding header')
  errors+=legacy.boot_bit_errors((image/'BOOT.BIN').read_bytes(),legacy.bit_payload((release/'hardware/top.bit').read_bytes()));errors+=legacy.boot_script_errors((image/'boot.scr').read_bytes())
  if 0x80000+(image/'Image').stat().st_size>=0x10000000 or 0x20000000+(image/'image.ub').stat().st_size>=0x40000000:errors.append('boot load overlap')
  result.update(files={n:dict(sha256=sha(image/n),bytes=(image/n).stat().st_size) for n in ('BOOT.BIN','boot.scr','image.ub','Image','system.dtb','rootfs.tar.gz')},binding_sha256=manifest['binding_sha256'],source_sha256=manifest['source_sha256'],release_manifest_sha256=sha(release/'manifest.json'),bit_sha256=manifest['digests']['bit'],xsa_sha256=manifest['digests']['xsa'])
 except Exception as exc:errors.append(str(exc))
 result.update(integrity='FAIL' if errors else 'PASS',errors=errors,exit=1 if errors else 0);js(out/'receipt.json',result)
 if errors:raise ValueError(str(errors))
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=('prepare','build','audit','preflight'));p.add_argument('--release',type=Path);p.add_argument('--project',type=Path,required=True);p.add_argument('--env',type=Path);a=p.parse_args()
 if a.kind!='preflight' and a.release is None:p.error('--release required for image work')
 r=preflight(a.project,a.env) if a.kind=='preflight' else audit(a.release,a.project) if a.kind=='audit' else (prepare if a.kind=='prepare' else build)(a.release,a.project,a.env);print(json.dumps(r));raise SystemExit(r.get('exit',0))
