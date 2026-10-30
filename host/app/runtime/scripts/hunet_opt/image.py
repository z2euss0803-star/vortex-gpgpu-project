"""Independent frozen-W4 software image. No RTL, board or media operations."""
import argparse,datetime,gzip,hashlib,importlib.util,io,json,pathlib,re,shutil,subprocess,sys,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT));sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import sha,write_json,native_output
POLICY='USER_20261014_UBUNTU_SOFTWARE_ONLY_FROZEN_HW4'
BIT='faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc'
XSA='08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536'
MIN_FREE=15*1024**3
CRYPTO_CONFIG=('CONFIG_CRYPTO=y','CONFIG_CRYPTO_HASH=y','CONFIG_CRYPTO_SHA256=y','CONFIG_KERNEL_MODE_NEON=y','CONFIG_ARM64_CRYPTO=y','CONFIG_CRYPTO_SHA256_ARM64=y','CONFIG_CRYPTO_SHA2_ARM64_CE=y')

def need(ok,why):
 if not ok:raise ValueError(why)

def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def parent_module():return load('opt_parent_image_pure',ROOT/'scripts/hunet/prepare_image.py')
def image_module():return load('opt_actual_image_pure',ROOT/'scripts/hunet/image_audit.py')
def content_errors(actual,expected):return ['IMAGE_CONTENT:'+k for k,v in expected.items() if actual.get(k)!=v]

def check_gates(manifest,software,cross):
 need(manifest.get('profile')==4 and manifest.get('program_id')==10,'OPT_PROFILE_PROGRAM')
 need(manifest.get('bit_sha256')==BIT and manifest.get('xsa_sha256')==XSA,'FROZEN_W4_REQUIRED')
 need(type(software) is dict and software.get('scope')=='UBUNTU_SOFTWARE_NOT_GPU_NOT_BOARD' and software.get('natural_exit')==0 and software.get('source_digest')==manifest['source_digest'],'UBUNTU_SOFTWARE_GATE')
 need(type(cross) is dict and cross.get('scope')=='OPT_KERNEL_AARCH64_BUILD' and cross.get('exit')==0 and cross.get('audit',{}).get('integrity')=='PASS' and not cross.get('error'),'ACTUAL_BOUND_KERNEL_GATE')
 need(all(cross.get(k)==manifest[k] for k in ('source_commit','source_digest')),'BOUND_KERNEL_SOURCE')
 return dict(policy=POLICY,hardware='FROZEN_HW4_REUSED_NOT_RETESTED',board='NOT_RUN',h0='NOT_RUN',h1='NOT_RUN',sd='NOT_TOUCHED')

def check_module(directory,receipt,binding):
 p=pathlib.Path(directory)/'source/driver/vxl_linux.ko'
 need(p.is_file() and not p.is_symlink() and sha(p)==receipt.get('module_sha256') and binding in p.read_bytes(),'ACTUAL_BOUND_MODULE_IDENTITY')
 return sha(p)

def deployment_gates(release):
 from scripts.hunet_opt.package_release import verify_release
 release=pathlib.Path(release).resolve();result=verify_release(release);need(result.get('integrity')=='PASS','OPT_SEAL:'+str(result))
 m=json.loads((release/'release-opt.json').read_bytes());software=json.loads((release/'software-validation.json').read_bytes())
 bound=release.parent.parent/'bound-kernel';cross=json.loads((bound/'receipt.json').read_bytes())
 gate=check_gates(m,software,cross);gate['bound_module_sha256']=check_module(bound,cross,(release/'binding-opt.bin').read_bytes())
 return result,m,gate

def builtin_config():return '\n'.join(('CONFIG_VXL_LINUX=y','CONFIG_ZONE_DMA32=y','CONFIG_DEBUG_FS=y')+CRYPTO_CONFIG)+'\n'

def recipes(commit,license_md5,candidate):
 need(re.fullmatch('[A-Z][A-Z0-9_]*',candidate) is not None,'CANDIDATE_RECIPE_PATH')
 r=parent_module().recipes(commit,license_md5)
 r['kernel']=r['kernel'].replace('file://vxl_hunet_source.h file://vxl-built-in.cfg','file://vxl_hunet_source.h file://vxl_hybrid_release.h file://vxl_opt_release.h file://vxl-built-in.cfg')
 extra=''.join('    install -m 0644 ${WORKDIR}/'+n+' ${S}/drivers/misc/vxl/driver/\n' for n in ('vxl_hybrid_release.h','vxl_opt_release.h'))
 r['kernel']=r['kernel'].replace('    sed -i',extra+'    sed -i',1)
 # BitBake preserves the escaped quotes for the compiler macro string token.
 r['kernel']=r['kernel'].replace("    grep -q 'vxl/driver/'", "    printf '%s\\n' 'ccflags-y += -DVXL_OPT_CATALOG_HEADER=\\\"../generated/hunet_opt/"+candidate+"/catalog.h\\\"' >> ${S}/drivers/misc/vxl/driver/Makefile\n    grep -q 'vxl/driver/'",1)
 r['apps']='''SUMMARY = "Exact source-bound optimized HUNet client and device data"
LICENSE = "CLOSED"
SRC_URI = "file://hunet-release.tar.gz;subdir=hunet-release"
S = "${WORKDIR}/hunet-release"
do_compile() {
    :
}
do_install() {
    install -d ${D}${libdir} ${D}${bindir} ${D}${datadir}/codex-hunet-opt
    install -m 0755 cpu/gelu.elf ${D}${bindir}/vxl_hunet_opt
    install -m 0755 cpu/libvortex-opt.so ${D}${libdir}/
    cp -R --no-preserve=ownership ${S}/. ${D}${datadir}/codex-hunet-opt/
    find ${D}${datadir}/codex-hunet-opt -type f -exec chmod 0644 {} +
    find ${D}${datadir}/codex-hunet-opt -type d -exec chmod 0755 {} +
}
PACKAGES =+ "${PN}-gpu-programs"
FILES_${PN}-gpu-programs = "${datadir}/codex-hunet-opt/base/program6/kernel.elf ${datadir}/codex-hunet-opt/base/program7/kernel.elf ${datadir}/codex-hunet-opt/program10/kernel.elf"
INSANE_SKIP_${PN}-gpu-programs = "arch"
RDEPENDS_${PN} += "${PN}-gpu-programs"
FILES_${PN} += "${libdir}/*.so ${datadir}/codex-hunet-opt"
FILES_${PN}-dev = ""
INSANE_SKIP_${PN} += "dev-so ldflags already-stripped"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
'''
 return r

def prepare_image(parent,release,out,cache_project=None):
 parent,release,out=map(lambda p:pathlib.Path(p).resolve(),(parent,release,out))
 module=parent_module();module.validate_paths(parent,out)
 need(shutil.disk_usage(str(parent)).free>=MIN_FREE,'WAIT_SPACE_15_GIB')
 checked,manifest,gate=deployment_gates(release)
 # Only our retained HUNet caches are eligible. No C/global directory cleanup.
 own=pathlib.Path(cache_project).resolve() if cache_project else parent.parent
 need(own.parent==parent.parent.parent and own.name.startswith('codex_linux_backend_hunet_v5_'),'CACHE_OWN_HUNET_FAMILY')
 ci=json.loads((own/'integration.json').read_bytes())
 need(ci.get('scope') in ('HW4_HUNET_FUSED_ISOLATED_IMAGE_BUILD','HW4_HUNET_ISOLATED_IMAGE_BUILD','HW4_HUNET_OPT_ISOLATED_IMAGE_BUILD') and ci.get('manifest',{}).get('profile')==4,'CACHE_PROFILE')
 caches=[own/'env_backend/build'/n for n in ('downloads','sstate-cache')]
 for p in caches:
  resolved=p.resolve();cache_owner=resolved.parent.parent.parent
  need(p.is_dir() and resolved.name==p.name and resolved.parent.name=='build' and resolved.parent.parent.name=='env_backend' and cache_owner.parent==parent.parent.parent and cache_owner.name.startswith('codex_linux_backend_hunet_v5_'),'CACHE_NATIVE_OWN_DIRS')
  cached=json.loads((cache_owner/'integration.json').read_bytes())
  need(cached.get('manifest',{}).get('profile')==4 and cached.get('scope') in ('HW4_HUNET_ISOLATED_IMAGE_BUILD','HW4_HUNET_FUSED_ISOLATED_IMAGE_BUILD','HW4_HUNET_OPT_ISOLATED_IMAGE_BUILD'),'CACHE_OWNER_PROFILE')
 need(sha(parent/'images/linux/BOOT.BIN') and sha(release/'base/hardware/gpgpu_with_bit.xsa')==XSA,'RETAINED_BOOT_AND_XSA')
 a=image_module();need(not a.boot_bit_errors((parent/'images/linux/BOOT.BIN').read_bytes(),a.bit_payload((release/'base/hardware/top.bit').read_bytes())),'PARENT_W4_BOOT_PAYLOAD')
 protected=module.protected_files(parent);native_output(out);write_json(out/'protected-original.json',protected)
 project=out/'env_backend'
 def ignore(path,names):return [n for n in names if n=='.git' or n.endswith('.log') or (pathlib.Path(path)==parent and n in ('build','images'))]
 shutil.copytree(str(parent),str(project),ignore=ignore);(project/'build').mkdir()
 source=out/'sealed-source-opt';source.mkdir()
 with tarfile.open(str(release/'source.tar.gz'),'r:gz') as tf:
  need(all(not m.issym() and not m.islnk() and not m.name.startswith('/') and '..' not in pathlib.PurePosixPath(m.name).parts for m in tf.getmembers()),'SOURCE_ARCHIVE_PATH')
  tf.extractall(str(source))
 from scripts.hunet_opt.package_release import validate_payload
 inventory=json.loads((release/'SOURCE_SHA256.json').read_bytes());validate_payload(source,inventory['files'])
 meta=project/'project-spec/meta-user'
 def write(name,text):
  p=meta/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
 r=recipes(manifest['source_commit'],hashlib.md5((source/'vendor/vortex/LICENSE').read_bytes()).hexdigest(),manifest['candidate'])
 write('recipes-kernel/linux/linux-xlnx_%.bbappend',r['kernel']);write('recipes-kernel/linux/files/vxl-built-in.cfg',builtin_config())
 write('recipes-apps/vxl-runtime/vxl-runtime.bb',r['apps'])
 kfiles=meta/'recipes-kernel/linux/files'
 shutil.copyfile(release/'source.tar.gz',kfiles/'vxl-source.tar.gz')
 for n in ('vxl_hunet_source.h','vxl_hybrid_release.h','vxl_opt_release.h'):shutil.copyfile(release/'build'/n,kfiles/n)
 apps=meta/'recipes-apps/vxl-runtime/files';apps.mkdir(exist_ok=True,parents=True)
 with tarfile.open(str(apps/'hunet-release.tar.gz'),'w:gz') as tf:
  for name in sorted(set(manifest['files'])|{'release-opt.json'}):
   data=(release/name).read_bytes();t=tarfile.TarInfo(name);t.size=len(data);t.mode=0o644;t.mtime=0;tf.addfile(t,io.BytesIO(data))
 build='HW4-HUNET-OPT-V1-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
 identity=dict(scope='HW4_HUNET_OPT_LINUX_CANDIDATE_V1',build_id=build,profile=4,program_id=10,candidate=manifest['candidate'],sd_variant=module.SD_VARIANT,**{k:manifest[k] for k in ('source_commit','source_digest','binding_sha256','bit_sha256','xsa_sha256')})
 key=parent/'project-spec/meta-user/recipes-apps/codex-firstboot/files/authorized_keys';need(key.is_file() and key.read_bytes().strip(),'TRUSTED_PUBLIC_KEY_REQUIRED')
 write('recipes-apps/codex-firstboot/codex-firstboot.bb',r['firstboot']);write('recipes-apps/codex-firstboot/files/codex-build-id',build+'\n')
 write_json(meta/'recipes-apps/codex-firstboot/files/codex-hunet-manifest.json',identity)
 shutil.copyfile(key,meta/'recipes-apps/codex-firstboot/files/authorized_keys')
 write('recipes-apps/codex-firstboot/files/codex-direct-network','#!/bin/sh\ncase "$1" in\nstart)\n ip link set dev eth0 up || exit $?\n ip addr replace 192.168.13.10/24 dev eth0\n ;;\nstop) : ;;\nesac\n')
 write('recipes-core/init-ifupdown/files/interfaces','auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet static\n    address 192.168.13.10\n    netmask 255.255.255.0\n')
 write('recipes-bsp/device-tree/files/system-user.dtsi',module.dt_source())
 for n in ('boot.cmd.default','boot.cmd.default.initrd'):write('recipes-bsp/u-boot/u-boot-zynq-scr/'+n,module.BOOT_COMMAND)
 cfg=project/'project-spec/configs/config';text,n=re.subn(r'^CONFIG_SUBSYSTEM_HOSTNAME=.*$','CONFIG_SUBSYSTEM_HOSTNAME="codex-hw4-hunet-opt"',cfg.read_text(),flags=re.M);need(n==1,'HOSTNAME_CONFIG');cfg.write_text(text)
 with (meta/'conf/petalinuxbsp.conf').open('a') as stream:stream.write(module.offline_settings())
 driver_make=(source/'driver/Makefile').read_text().replace('obj-m += vxl_linux.o','obj-$(CONFIG_VXL_LINUX) += vxl_linux.o')
 driver_make=re.sub(r'^ccflags-y \+= -I.* -Werror$','ccflags-y += -I$(srctree)/drivers/misc/vxl/include -Werror',driver_make,flags=re.M)
 driver_make+='ccflags-y += -DVXL_OPT_CATALOG_HEADER=\\"../generated/hunet_opt/'+manifest['candidate']+'/catalog.h\\"\n'
 shutil.copyfile(parent/'images/linux/BOOT.BIN',out/'retained-BOOT.BIN')
 integration=dict(scope='HW4_HUNET_OPT_ISOLATED_IMAGE_BUILD',policy=POLICY,manifest=identity,source_project=str(parent),derived_project=str(project),release_dir=str(release),trusted_public_key=str(key),kernel_makefile=driver_make,cache_mode='OWN_HW4_SHARED',cache_project=str(own),boot_policy='RETAIN_EXACT_FROZEN_W4_BOOT',retained_boot_sha256=sha(out/'retained-BOOT.BIN'),sealed_source=str(source),gate=gate)
 write_json(out/'integration.json',integration)
 for cache in caches:(project/'build'/cache.name).symlink_to(cache,target_is_directory=True)
 need(all(sha(pathlib.Path(p))==want for p,want in protected.items()),'PARENT_MUTATED')
 return integration

def audit_image(image,release,out):
 image,release=pathlib.Path(image).resolve(),pathlib.Path(release).resolve();out=native_output(out)
 result=dict(scope='HW4_HUNET_OPT_ACTUAL_IMAGE_AUDIT',integrity='FAIL',policy=POLICY,hardware='FROZEN_HW4_REUSED_NOT_RETESTED',board='NOT_RUN',h0='NOT_RUN',h1='NOT_RUN',sd='NOT_TOUCHED',gpu_cycles=None);errors=[]
 try:
  verified,manifest,gate=deployment_gates(release);project=image.parent.parent;candidate=project.parent;integration=json.loads((candidate/'integration.json').read_bytes());m=integration['manifest'];a=image_module()
  need(integration['policy']==POLICY and (candidate/'build.exit').read_text().strip()=='0','ACTUAL_IMAGE_BUILD_EXIT_POLICY')
  for p,want in json.loads((candidate/'protected-original.json').read_bytes()).items():need(sha(pathlib.Path(p))==want,'PARENT_CHANGED:'+p)
  need(all(m.get(k)==manifest[k] for k in ('source_commit','source_digest','binding_sha256','bit_sha256','xsa_sha256','candidate')) and m['build_id'].startswith('HW4-HUNET-OPT-V1-'),'IMAGE_MANIFEST')
  kernels=list((project/'build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx').glob('*-r0/linux-*-build'));need(len(kernels)==1,'ACTUAL_KERNEL_AMBIGUOUS');kernel=kernels[0]
  native=kernel.parent/'recipe-sysroot-native/usr/bin';fdtget=str(native/'fdtget');dumpimage=str(native/'dumpimage')
  for index,name in enumerate(('fit-kernel.bin','fit-system.dtb','fit-rootfs.bin')):subprocess.check_call([dumpimage,'-T','flat_dt','-p',str(index),'-o',str(out/name),str(image/'image.ub')],stdout=subprocess.DEVNULL)
  fit=gzip.decompress((out/'fit-kernel.bin').read_bytes())==(image/'Image').read_bytes() and (out/'fit-system.dtb').read_bytes()==(image/'system.dtb').read_bytes() and (out/'fit-rootfs.bin').read_bytes()==(image/'rootfs.cpio.gz').read_bytes()
  for node,name in [('kernel@1','fit-kernel.bin'),('fdt@system-top.dtb','fit-system.dtb'),('ramdisk@1','fit-rootfs.bin')]:
   base='/images/'+node;children=subprocess.check_output([fdtget,'-l',str(image/'image.ub'),base],universal_newlines=True).split();checks=0
   for child in children:
    path=base+'/'+child
    if subprocess.check_output([fdtget,'-t','s',str(image/'image.ub'),path,'algo'],universal_newlines=True).strip()=='sha256':
     value=subprocess.check_output([fdtget,'-t','bx',str(image/'image.ub'),path,'value'],universal_newlines=True).split();fit=fit and bytes(int(x,16) for x in value).hex()==sha(out/name);checks+=1
   fit=fit and checks==1
  cpio=gzip.decompress((out/'fit-rootfs.bin').read_bytes());old=a.legacy_cpio();entries=old.cpio_entries(cpio);files=a.resolved_regular_files(entries)
  recipe=project/'components/yocto/layers/core/meta/classes/image_types.bbclass';errors+=old.rootfs_payload_errors(cpio,image/'rootfs.tar.gz',sha(recipe));need(sha(recipe)==old.CPIO_INIT_RECIPE_SHA256,'UNRECOGNIZED_INIT_RECIPE')
  prefix='usr/share/codex-hunet-opt/'
  for name,record in manifest['files'].items():
   raw=files.get(prefix+name,b'')
   if len(raw)!=record['bytes'] or hashlib.sha256(raw).hexdigest()!=record['sha256']:errors.append('INSTALLED_RELEASE:'+name)
  need(files.get(prefix+'release-opt.json')==(release/'release-opt.json').read_bytes(),'INSTALLED_MANIFEST')
  config=(kernel/'.config').read_text().splitlines();symbols=(kernel/'System.map').read_text();kernel_bytes=(image/'Image').read_bytes();binding=(release/'binding-opt.bin').read_bytes()
  builtins=('vxl_opt_init','vxl_opt_prepare_control','vxl_opt_gpu_completed','vxl_kernel_crypto_ops')
  builtin=all(re.search(r'^\S+\s+[Tt]\s+'+n+r'$',symbols,re.M) for n in builtins) and all(x in config for x in builtin_config().splitlines())
  crypto_objects={n:sha(kernel/'arch/arm64/crypto'/n) for n in ('sha2-ce-core.o','sha2-ce-glue.o','sha256-core.o','sha256-glue.o')}
  inventory=json.loads((release/'SOURCE_SHA256.json').read_bytes())['files'];actual=project/'build/tmp/work-shared/zynqmp-generic/kernel-source/drivers/misc/vxl';source_ok=True
  for name,want in inventory.items():
   if name.split('/')[0] not in ('driver','core','include','generated') or name=='driver/Makefile':continue
   if not (actual/name).is_file() or sha(actual/name)!=want['sha256']:source_ok=False
  if (actual/'driver/Makefile').read_text()!=integration['kernel_makefile']:source_ok=False
  for name in ('vxl_hunet_source.h','vxl_hybrid_release.h','vxl_opt_release.h'):
   if (actual/'driver'/name).read_bytes()!=(release/'build'/name).read_bytes():source_ok=False
  libm=json.loads((release/'cpu/runtime.json').read_bytes())['libm_sha256']
  expected=dict(binding=manifest['binding_sha256'],cpu=sha(release/'cpu/gelu.elf'),libm=libm,library=sha(release/'cpu/libvortex-opt.so'),program=sha(release/'program10/code.bin'),boot=integration['retained_boot_sha256'],builtin=True,crypto=True,full_source=True,fit=True,dt=True,ssh=True)
  digest=lambda name:hashlib.sha256(files.get(name,b'')).hexdigest()
  observed=dict(binding=hashlib.sha256(binding).hexdigest() if binding in kernel_bytes and binding in (image/'vmlinux').read_bytes() else None,cpu=digest('usr/bin/vxl_hunet_opt'),library=digest('usr/lib/libvortex-opt.so'),libm=digest('lib/libm.so.6'),program=digest(prefix+'program10/code.bin'),boot=sha(image/'BOOT.BIN'),builtin=bool(builtin),crypto=all(x in config for x in CRYPTO_CONFIG) and len(crypto_objects)==4,full_source=source_ok,fit=fit,dt=not a.dt_errors(a.dt_properties(image/'system.dtb',fdtget)),ssh=not a.ssh_entry_errors(entries))
  errors+=content_errors(observed,expected);errors+=a.boot_bit_errors((image/'BOOT.BIN').read_bytes(),a.bit_payload((release/'base/hardware/top.bit').read_bytes()));errors+=a.boot_script_errors((image/'boot.scr').read_bytes())
  if files.get('etc/codex-build-id',b'').decode().strip()!=m['build_id'] or files.get('home/root/.ssh/authorized_keys')!=pathlib.Path(integration['trusted_public_key']).read_bytes():errors.append('IDENTITY_RECOVERY')
  need(json.loads(files.get('etc/codex-hunet-manifest.json',b'{}'))==m,'ROOTFS_BUILD_MANIFEST')
  if 0x80000+len(kernel_bytes)>=0x10000000 or 0x20000000+(image/'image.ub').stat().st_size>=0x40000000:errors.append('BOOT_LOAD_COLLISION')
  result.update(observed=observed,expected=expected,source_commit=manifest['source_commit'],source_digest=manifest['source_digest'],binding_sha256=manifest['binding_sha256'],candidate=manifest['candidate'],build_id=m['build_id'],crypto_objects=crypto_objects,files={n:dict(bytes=(image/n).stat().st_size,sha256=sha(image/n)) for n in ('BOOT.BIN','boot.scr','image.ub','Image','rootfs.tar.gz','system.dtb')})
 except Exception as e:errors.append(str(e))
 result.update(errors=errors,integrity='FAIL' if errors else 'PASS');write_json(out/'receipt.json',result)
 if errors:raise ValueError(errors)
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=('prepare','audit'));p.add_argument('--parent',type=pathlib.Path);p.add_argument('--release',type=pathlib.Path,required=True);p.add_argument('--image',type=pathlib.Path);p.add_argument('--cache-project',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path,required=True);args=p.parse_args()
 result=prepare_image(args.parent,args.release,args.out,args.cache_project) if args.kind=='prepare' else audit_image(args.image,args.release,args.out)
 print(json.dumps(result,indent=2))
