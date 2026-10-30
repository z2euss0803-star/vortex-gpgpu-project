"""Fresh PetaLinux candidate, fail-closed gates and actual image audit. No SD writes."""
import argparse,datetime,gzip,hashlib,importlib.util,io,json,pathlib,re,shutil,subprocess,sys,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import sha,write_json,native_output
MIN_FREE=15717892096
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def pkg():return load('fused_image_package',ROOT/'scripts/hunet_fused/package_release.py')
def parent_module():return load('fused_parent_image',ROOT/'scripts/hunet/prepare_image.py')
def image_module():return load('fused_actual_image',ROOT/'scripts/hunet/image_audit.py')
def space_gate(available):
 if type(available) is not int or available<MIN_FREE:raise ValueError('WAIT_SPACE:available=%s required=%s'%(available,MIN_FREE))
 return available
def content_errors(actual,expected):return ['IMAGE_CONTENT:'+k for k,v in expected.items() if actual.get(k)!=v]

def checked_boot_sha(integration,manifest,receipt):
 if integration.get('boot_policy')!='REBUILD_MATCHED_W4':return integration['retained_boot_sha256']
 if type(receipt) is not dict or receipt.get('scope')!='MATCHED_W4_BOOT_PACKAGE_V1' or type(receipt.get('exit')) is not int or receipt['exit']!=0:raise ValueError('MATCHED_BOOT_PACKAGE_REQUIRED')
 if any(receipt.get(k)!=manifest[k] for k in ('bit_sha256','xsa_sha256')):raise ValueError('BOOT_PLATFORM_MISMATCH')
 if not re.fullmatch('[a-f0-9]{64}',receipt.get('boot_sha256','')) or set(receipt.get('inputs',{}))!={'fsbl','pmufw','atf','uboot'} or any(not re.fullmatch('[a-f0-9]{64}',x) for x in receipt['inputs'].values()):raise ValueError('BOOT_PACKAGE_IDENTITY')
 return receipt['boot_sha256']
def checked_boot_files(project,release,integration,receipt):
 project,release=pathlib.Path(project),pathlib.Path(release)
 manifest=json.loads((release/'release-fused.json').read_bytes());expected=checked_boot_sha(integration,manifest,receipt)
 if integration.get('boot_policy')=='REBUILD_MATCHED_W4':
  names=dict(fsbl='zynqmp_fsbl.elf',pmufw='pmufw.elf',atf='bl31.elf',uboot='u-boot.elf')
  for role,name in names.items():
   p=project/'images/linux'/name
   if p.is_symlink() or sha(p)!=receipt['inputs'][role]:raise ValueError('BOOT_ACTUAL_FIRMWARE:'+role)
  imports=list((project/'project-spec/hw-description').glob('*.xsa'))
  if len(imports)!=1 or imports[0].is_symlink() or sha(imports[0])!=manifest['xsa_sha256']:raise ValueError('BOOT_ACTUAL_IMPORTED_XSA')
 return expected

def recipes(commit,license_md5):
 r=parent_module().recipes(commit,license_md5)
 r['kernel']=r['kernel'].replace('file://vxl_hunet_source.h file://vxl-built-in.cfg','file://vxl_hunet_source.h file://vxl_hybrid_release.h file://vxl_fused_release.h file://vxl-built-in.cfg')
 needle='    sed -i';extra=''.join('    install -m 0644 ${WORKDIR}/'+n+' ${S}/drivers/misc/vxl/driver/\n' for n in ('vxl_hybrid_release.h','vxl_fused_release.h'))
 r['kernel']=r['kernel'].replace(needle,extra+needle,1)
 r['apps']='''SUMMARY = "Exact source-bound HUNet stage-fusion client and device payloads"
LICENSE = "CLOSED"
SRC_URI = "file://hunet-release.tar.gz;subdir=hunet-release"
S = "${WORKDIR}/hunet-release"
do_compile() { :; }
do_install() {
    install -d ${D}${libdir} ${D}${bindir} ${D}${datadir}/codex-hunet-fused
    install -m 0755 cpu/gelu.elf ${D}${bindir}/vxl_hunet_fused
    install -m 0755 cpu/libvortex-fused.so ${D}${libdir}/
    cp -R --no-preserve=ownership ${S}/. ${D}${datadir}/codex-hunet-fused/
    find ${D}${datadir}/codex-hunet-fused -type f -exec chmod 0644 {} +
    find ${D}${datadir}/codex-hunet-fused -type d -exec chmod 0755 {} +
}
PACKAGES =+ "${PN}-gpu-programs"
FILES_${PN}-gpu-programs = "${datadir}/codex-hunet-fused/base/program6/kernel.elf ${datadir}/codex-hunet-fused/base/program7/kernel.elf ${datadir}/codex-hunet-fused/program8/kernel.elf ${datadir}/codex-hunet-fused/program9/kernel.elf"
INSANE_SKIP_${PN}-gpu-programs = "arch"
RDEPENDS_${PN} += "${PN}-gpu-programs"
FILES_${PN} += "${libdir}/*.so ${datadir}/codex-hunet-fused"
FILES_${PN}-dev = ""
INSANE_SKIP_${PN} += "dev-so ldflags already-stripped"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
'''
 return r
def deployment_gates(release):
 release=pathlib.Path(release).resolve();p=pkg();result=p.verify_fused_release(release)
 if result['integrity']!='PASS':raise ValueError(result)
 runner=load('fused_rtl_gate',ROOT/'scripts/hunet_fused/rtl_check.py');rtl=runner.validate_artifacts(release.parent/'rtl')
 if rtl['elf_sha256']!=sha(release/'program9/kernel.elf'):raise ValueError('RTL_ELF_IDENTITY')
 cross=load('fused_cross_gate',ROOT/'scripts/hunet_fused/cross.py');directory=release.parent/'cross';receipt=json.loads((directory/'receipt.json').read_bytes());cross.audit_cross_files(directory,receipt,(release/'binding-fused.bin').read_bytes())
 if any(receipt.get(k)!=result[k] for k in ('source_commit','source_digest','binding_sha256')):raise ValueError('CROSS_SOURCE_IDENTITY')
 return result
def prepare_image(parent,release,out,cache_project=None):
 parent,release,out=map(lambda p:pathlib.Path(p).resolve(),(parent,release,out))
 space_gate(shutil.disk_usage(parent).free);verified=deployment_gates(release)
 if out.exists() or out==parent or parent in out.parents or out in parent.parents:raise ValueError('FRESH_SEPARATE_PROJECT_REQUIRED')
 module=parent_module();integration=module.prepare_image(parent,out,release/'base',release/'base/hardware/gpgpu_with_bit.xsa',cache_project)
 source=out/'sealed-source-fused';cross=load('fused_image_snapshot',ROOT/'scripts/hunet_fused/cross.py');cross.snapshot(release,source)
 meta=out/'env_backend/project-spec/meta-user';r=recipes(verified['source_commit'],hashlib.md5((source/'vendor/vortex/LICENSE').read_bytes()).hexdigest())
 (meta/'recipes-kernel/linux/linux-xlnx_%.bbappend').write_text(r['kernel']);(meta/'recipes-apps/vxl-runtime/vxl-runtime.bb').write_text(r['apps'])
 for n in ('vxl_hybrid_release.h','vxl_fused_release.h'):shutil.copyfile(release/'build'/n,meta/'recipes-kernel/linux/files'/n)
 for path in ('recipes-kernel/linux/files/vxl-source.tar.gz','recipes-apps/vxl-runtime/files/vxl-source.tar.gz'):shutil.copyfile(release/'source.tar.gz',meta/path)
 manifest=json.loads((release/'release-fused.json').read_bytes());archive=meta/'recipes-apps/vxl-runtime/files/hunet-release.tar.gz'
 with tarfile.open(str(archive),'w:gz') as tar:
  for name in sorted(set(manifest['files'])|{'release-fused.json'}):
   data=(release/name).read_bytes();t=tarfile.TarInfo(name);t.size=len(data);t.mode=0o644;t.mtime=0;tar.addfile(t,io.BytesIO(data))
 build='HW4-HUNET-FUSED-V1-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
 identity=dict(scope='HW4_HUNET_FUSED_LINUX_CANDIDATE_V1',build_id=build,program_id=9,profile=4,sd_variant=module.SD_VARIANT,**{k:manifest[k] for k in ('source_commit','source_digest','binding_sha256','bit_sha256','xsa_sha256')})
 first=meta/'recipes-apps/codex-firstboot/files';(first/'codex-build-id').write_text(build+'\n');write_json(first/'codex-hunet-manifest.json',identity)
 cfg=out/'env_backend/project-spec/configs/config';text,n=re.subn(r'^CONFIG_SUBSYSTEM_HOSTNAME=.*$','CONFIG_SUBSYSTEM_HOSTNAME="codex-hw4-hunet-fused"',cfg.read_text(),flags=re.M)
 if n!=1:raise ValueError('HOSTNAME_CONFIG')
 cfg.write_text(text);boot=parent/'images/linux/BOOT.BIN'
 if manifest['bit_sha256']=='faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc':integration['boot_policy']='REBUILD_MATCHED_W4'
 else:shutil.copyfile(boot,out/'retained-BOOT.BIN')
 integration.update(scope='HW4_HUNET_FUSED_ISOLATED_IMAGE_BUILD',manifest=identity,release_dir=str(release),sealed_source=str(source),retained_boot_sha256=sha(boot),space_gate=MIN_FREE,cpu_install='EXACT_RELEASE_ELF_NO_STRIP_NO_RECOMPILE')
 write_json(out/'integration.json',integration);return integration
def audit_image(image,release,out):
 image,release=pathlib.Path(image).resolve(),pathlib.Path(release).resolve();out=native_output(out);result=dict(scope='HW4_HUNET_FUSED_ACTUAL_IMAGE_AUDIT',integrity='FAIL',board='NOT_RUN',sd='NOT_TOUCHED',h1='NOT_RUN');errors=[]
 try:
  deployment_gates(release);manifest=json.loads((release/'release-fused.json').read_bytes());project=image.parent.parent;candidate=project.parent;integration=json.loads((candidate/'integration.json').read_bytes());m=integration['manifest'];a=image_module()
  for path in ('build.exit','package.exit'):
   if (candidate/path).read_text().strip()!='0':raise ValueError('ACTUAL_IMAGE_BUILD_EXIT')
  for name,want in json.loads((candidate/'protected-original.json').read_bytes()).items():
   if sha(pathlib.Path(name))!=want:raise ValueError('PARENT_CHANGED:'+name)
  if any(m.get(k)!=manifest[k] for k in ('source_commit','source_digest','binding_sha256','bit_sha256','xsa_sha256')) or not m.get('build_id','').startswith('HW4-HUNET-FUSED-V1-'):raise ValueError('IMAGE_MANIFEST')
  kernels=list((project/'build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx').glob('*-r0/linux-*-build'))
  if len(kernels)!=1:raise ValueError('ACTUAL_KERNEL_AMBIGUOUS')
  kernel=kernels[0];native=kernel.parent/'recipe-sysroot-native/usr/bin';fdtget=str(native/'fdtget');dumpimage=str(native/'dumpimage')
  for index,name in enumerate(('fit-kernel.bin','fit-system.dtb','fit-rootfs.bin')):subprocess.check_call([dumpimage,'-T','flat_dt','-p',str(index),'-o',str(out/name),str(image/'image.ub')],stdout=subprocess.DEVNULL)
  fit=(gzip.decompress((out/'fit-kernel.bin').read_bytes())==(image/'Image').read_bytes() and (out/'fit-system.dtb').read_bytes()==(image/'system.dtb').read_bytes() and (out/'fit-rootfs.bin').read_bytes()==(image/'rootfs.cpio.gz').read_bytes())
  for node,name in [('kernel@1','fit-kernel.bin'),('fdt@system-top.dtb','fit-system.dtb'),('ramdisk@1','fit-rootfs.bin')]:
   base='/images/'+node;children=subprocess.check_output([fdtget,'-l',str(image/'image.ub'),base],universal_newlines=True).split();checks=0
   for child in children:
    path=base+'/'+child
    if subprocess.check_output([fdtget,'-t','s',str(image/'image.ub'),path,'algo'],universal_newlines=True).strip()=='sha256':
     value=subprocess.check_output([fdtget,'-t','bx',str(image/'image.ub'),path,'value'],universal_newlines=True).split();fit=fit and bytes(int(x,16) for x in value).hex()==sha(out/name);checks+=1
   fit=fit and checks==1
  cpio=gzip.decompress((out/'fit-rootfs.bin').read_bytes());old=a.legacy_cpio();entries=old.cpio_entries(cpio);files=a.resolved_regular_files(entries)
  recipe=project/'components/yocto/layers/core/meta/classes/image_types.bbclass';errors+=old.rootfs_payload_errors(cpio,image/'rootfs.tar.gz',sha(recipe))
  if sha(recipe)!=old.CPIO_INIT_RECIPE_SHA256:errors.append('UNRECOGNIZED_INIT_RECIPE')
  for name,record in manifest['files'].items():
   raw=files.get('usr/share/codex-hunet-fused/'+name,b'')
   if len(raw)!=record['bytes'] or hashlib.sha256(raw).hexdigest()!=record['sha256']:errors.append('INSTALLED_RELEASE:'+name)
  installed=files.get('usr/share/codex-hunet-fused/release-fused.json',b'')
  if installed!=(release/'release-fused.json').read_bytes():errors.append('INSTALLED_MANIFEST')
  cpu_sha=sha(release/'cpu/gelu.elf');libm=json.loads((release/'cpu/runtime.json').read_bytes())['libm_sha256'];config=(kernel/'.config').read_text().splitlines();symbols=(kernel/'System.map').read_text();kernel_bytes=(image/'Image').read_bytes();binding=(release/'binding-fused.bin').read_bytes()
  builtins=('vxl_fused_init','vxl_fused_check_program','vxl_fused_gpu_completed','vxl_fused_owner_trace')
  builtin=all(re.search(r'^\S+\s+[Tt]\s+'+name+r'$',symbols,re.M) for name in builtins) and all(x in config for x in ('CONFIG_VXL_LINUX=y','CONFIG_ZONE_DMA32=y','CONFIG_CRYPTO_SHA256=y'))
  inventory=json.loads((release/'SOURCE_SHA256.json').read_bytes())['files'];actual=project/'build/tmp/work-shared/zynqmp-generic/kernel-source/drivers/misc/vxl';source_ok=True
  for name,want in inventory.items():
   if name.split('/')[0] not in ('driver','core','include','generated'):continue
   if name=='driver/Makefile':continue
   if not (actual/name).is_file() or sha(actual/name)!=want['sha256']:source_ok=False
  if (actual/'driver/Makefile').read_text()!=integration['kernel_makefile']:source_ok=False
  for name in ('vxl_hybrid_release.h','vxl_fused_release.h'):
   if (actual/'driver'/name).read_bytes()!=(release/'build'/name).read_bytes():source_ok=False
  boot_receipt=json.loads((candidate/'boot-package-receipt.json').read_bytes()) if integration.get('boot_policy')=='REBUILD_MATCHED_W4' else None
  expected=dict(binding=manifest['binding_sha256'],cpu=cpu_sha,libm=libm,library=sha(release/'cpu/libvortex-fused.so'),program=sha(release/'program9/code.bin'),boot=checked_boot_files(project,release,integration,boot_receipt),builtin=True,full_source=True,fit=True,dt=True,ssh=True)
  observed=dict(binding=hashlib.sha256(binding).hexdigest() if binding in kernel_bytes and binding in (image/'vmlinux').read_bytes() else None,cpu=hashlib.sha256(files.get('usr/bin/vxl_hunet_fused',b'')).hexdigest(),libm=hashlib.sha256(files.get('lib/libm.so.6',b'')).hexdigest(),program=hashlib.sha256(files.get('usr/share/codex-hunet-fused/program9/code.bin',b'')).hexdigest(),boot=sha(image/'BOOT.BIN'),builtin=bool(builtin),full_source=source_ok,fit=fit,dt=not a.dt_errors(a.dt_properties(image/'system.dtb',fdtget)),ssh=not a.ssh_entry_errors(entries))
  observed['library']=hashlib.sha256(files.get('usr/lib/libvortex-fused.so',b'')).hexdigest()
  errors+=content_errors(observed,expected);errors+=a.boot_bit_errors((image/'BOOT.BIN').read_bytes(),a.bit_payload((release/'base/hardware/top.bit').read_bytes()));errors+=a.boot_script_errors((image/'boot.scr').read_bytes())
  if files.get('etc/codex-build-id',b'').decode().strip()!=m['build_id'] or files.get('home/root/.ssh/authorized_keys')!=pathlib.Path(integration['trusted_public_key']).read_bytes():errors.append('IDENTITY_RECOVERY')
  if 0x80000+len(kernel_bytes)>=0x10000000 or 0x20000000+(image/'image.ub').stat().st_size>=0x40000000:errors.append('BOOT_LOAD_COLLISION')
  result.update(observed=observed,expected=expected,build_id=m['build_id'],files={n:dict(bytes=(image/n).stat().st_size,sha256=sha(image/n)) for n in ('BOOT.BIN','boot.scr','image.ub','Image','rootfs.tar.gz','system.dtb')})
 except Exception as e:errors.append(str(e))
 result.update(errors=errors,integrity='FAIL' if errors else 'PASS');write_json(out/'receipt.json',result)
 if errors:raise ValueError(errors)
 print('FUSED_IMAGE_AUDIT_PASS_NOT_BOARD',out,flush=True);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=('prepare','audit'));p.add_argument('--parent',type=pathlib.Path);p.add_argument('--release',type=pathlib.Path,required=True);p.add_argument('--image',type=pathlib.Path);p.add_argument('--cache-project',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
 r=prepare_image(a.parent,a.release,a.out,a.cache_project) if a.kind=='prepare' else audit_image(a.image,a.release,a.out);print(json.dumps(r,indent=2))
