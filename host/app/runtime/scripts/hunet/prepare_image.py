"""Prepare a fresh native HW4 candidate; never write boot media or the parent."""
import argparse
import datetime
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from build_common import checked_release, native_output, sha, source_snapshot, write_json
from package_release import file_bytes

SD_VARIANT = 'sd-compat-two-flags-trial-a1'
BOOT_COMMAND = '''setenv fdt_high 0x0fffffff
setenv initrd_high 0x3fffffff
setenv bootargs earlycon console=ttyPS0,115200 root=/dev/ram0 rw
if fatload mmc 1:1 0x20000000 image.ub; then
    bootm 0x20000000
fi
exit 1
'''


def validate_paths(base_dir, candidate_dir):
    old = Path(base_dir).resolve(); new = Path(candidate_dir).resolve()
    if not old.is_dir() or old.name != 'env_backend':
        raise ValueError('BASE_PROJECT_REQUIRED')
    if new.parent != old.parent.parent or not new.name.startswith('codex_linux_backend_hunet_v5_'):
        raise ValueError('FRESH_SIBLING_HW4_CANDIDATE_REQUIRED')
    if new == old.parent or new in old.parents or old in new.parents or new.exists():
        raise ValueError('PARENT_OVERLAP_OR_EXISTING_CANDIDATE')
    return old, new


def recipes(commit, license_md5):
    if not re.fullmatch('[0-9a-f]{40}', commit) or not re.fullmatch('[0-9a-f]{32}', license_md5):
        raise ValueError('RECIPE_SOURCE_IDENTITY')
    kernel = '''FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += " file://vxl-source.tar.gz;subdir=vxl-source file://vxl_hunet_source.h file://vxl-built-in.cfg"
do_patch_append() {
    install -d ${S}/drivers/misc/vxl
    cp -a ${WORKDIR}/vxl-source/driver ${WORKDIR}/vxl-source/core ${WORKDIR}/vxl-source/include ${WORKDIR}/vxl-source/generated ${S}/drivers/misc/vxl/
    install -m 0644 ${WORKDIR}/vxl_hunet_source.h ${S}/drivers/misc/vxl/driver/
    sed -i 's/^obj-m += vxl_linux.o/obj-$(CONFIG_VXL_LINUX) += vxl_linux.o/' ${S}/drivers/misc/vxl/driver/Makefile
    sed -i 's|^ccflags-y += -I.* -Werror|ccflags-y += -I$(srctree)/drivers/misc/vxl/include -Werror|' ${S}/drivers/misc/vxl/driver/Makefile
    grep -q 'vxl/driver/' ${S}/drivers/misc/Makefile || printf '\\nobj-$(CONFIG_VXL_LINUX) += vxl/driver/\\n' >> ${S}/drivers/misc/Makefile
    grep -q 'vxl/driver/Kconfig' ${S}/drivers/misc/Kconfig || printf '\\nsource "drivers/misc/vxl/driver/Kconfig"\\n' >> ${S}/drivers/misc/Kconfig
}
'''
    apps = '''SUMMARY = "Frozen HW4 HUNet v5 Linux client and retained raw contracts"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://vendor/vortex/LICENSE;md5=@LICENSE@"
SRC_URI = "file://vxl-source.tar.gz;subdir=vxl-source file://vxl_hunet_source.h file://hunet-release.tar.gz;subdir=hunet-release"
S = "${WORKDIR}/vxl-source"
DEPENDS += "python3-native"
do_compile() {
    oe_runmake OUT=build CC="${CC}" CXX="${CXX}" SOURCE_COMMIT=@COMMIT@ HUNET_BINDING_HEADER="${WORKDIR}/vxl_hunet_source.h" build/libvortex-hunet.so build/vxl_hunet build/vxl_hunet_copy build/vxl_hunet_wmma
}
do_install() {
    install -d ${D}${libdir} ${D}${bindir} ${D}${datadir}/codex-hunet-v5
    install -m 0755 build/libvortex-hunet.so ${D}${libdir}/
    install -m 0755 build/vxl_hunet build/vxl_hunet_copy build/vxl_hunet_wmma ${D}${bindir}/
    cp -R --no-preserve=ownership ${WORKDIR}/hunet-release/. ${D}${datadir}/codex-hunet-v5/
    find ${D}${datadir}/codex-hunet-v5 -type f -exec chmod 0644 {} +
    find ${D}${datadir}/codex-hunet-v5 -type d -exec chmod 0755 {} +
}
# RV32 GPU programs are immutable device payloads, not AArch64 executables.
# Claim only these two files before the host runtime package takes the data tree.
PACKAGES =+ "${PN}-gpu-programs"
FILES_${PN}-gpu-programs = "${datadir}/codex-hunet-v5/program6/kernel.elf ${datadir}/codex-hunet-v5/program7/kernel.elf"
INSANE_SKIP_${PN}-gpu-programs = "arch"
RDEPENDS_${PN} += "${PN}-gpu-programs"
FILES_${PN} += "${libdir}/*.so ${datadir}/codex-hunet-v5"
FILES_${PN}-dev = ""
INSANE_SKIP_${PN} += "dev-so ldflags already-stripped"
'''.replace('@LICENSE@', license_md5).replace('@COMMIT@', commit)
    firstboot = '''SUMMARY = "HW4 candidate identity and trusted public-key access"
LICENSE = "CLOSED"
SRC_URI = "file://codex-direct-network file://authorized_keys file://codex-build-id file://codex-hunet-manifest.json"
S = "${WORKDIR}"
inherit update-rc.d
INITSCRIPT_NAME = "codex-direct-network"
INITSCRIPT_PARAMS = "start 45 S ."
do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/codex-direct-network ${D}${sysconfdir}/init.d/codex-direct-network
    install -d -m 0755 ${D}/home/root
    install -d -m 0700 ${D}/home/root/.ssh
    install -m 0600 ${WORKDIR}/authorized_keys ${D}/home/root/.ssh/authorized_keys
    install -m 0644 ${WORKDIR}/codex-build-id ${D}${sysconfdir}/codex-build-id
    install -m 0644 ${WORKDIR}/codex-hunet-manifest.json ${D}${sysconfdir}/codex-hunet-manifest.json
}
FILES_${PN} += "/home/root/.ssh /home/root/.ssh/authorized_keys"
'''
    return dict(kernel=kernel, apps=apps, firstboot=firstboot)


def release_archive(release_dir, destination, manifest):
    with tarfile.open(str(destination), 'w:gz') as archive:
        for name in sorted(set(manifest['files']) | {'release.json'}):
            data = file_bytes(release_dir, name)
            entry = tarfile.TarInfo(name);entry.size=len(data);entry.mode=0o644;entry.mtime=0
            archive.addfile(entry, io.BytesIO(data))


def protected_files(old):
    return {str(p): sha(p) for folder in ('project-spec/configs', 'project-spec/meta-user', 'images/linux')
            for p in (old/folder).rglob('*') if p.is_file()}


def dt_source():
    return '''/include/ "system-conf.dtsi"
/ {
    reserved-memory {
        #address-cells = <2>; #size-cells = <2>; ranges;
        vxl_ps_hw4: vortex-ps@10000000 {
            compatible = "shared-dma-pool";
            reg = <0 0x10000000 0 0x00100000>;
            no-map;
        };
    };
};
&{/amba_pl@0/HPM@a0000000} { status = "disabled"; };
&{/amba_pl@0/dma@b0010000} { status = "disabled"; };
&{/amba_pl@0/gpio@b0020000} {
    clocks = <&{/firmware/zynqmp-firmware/clock-controller} 71>;
};
&{/amba_pl@0} {
    vortex-backend@a0000000 {
        compatible = "vortex,hw4-hunet-v5";
        reg = <0 0xa0000000 0 0x10000>, <0 0xb0010000 0 0x10000>;
        reg-names = "gpu", "cdma";
        clocks = <&{/firmware/zynqmp-firmware/clock-controller} 71>;
        clock-names = "axi";
        resets = <&{/firmware/zynqmp-firmware/reset-controller} 116>;
        ready-gpios = <&{/amba_pl@0/gpio@b0020000} 0 0 0>;
        memory-region = <&vxl_ps_hw4>;
        vortex,ps-placement-approved;
        status = "okay";
    };
};
&{/chosen} { bootargs = "earlycon console=ttyPS0,115200 root=/dev/ram0 rw"; };
&sdhci1 {
    no-1-8-v;
    disable-wp;
    codex,sd-compat-variant = "sd-compat-two-flags-trial-a1";
};
'''


def cache_directories(old,candidate,cache_project=None):
    old=Path(old).resolve();candidate=Path(candidate).resolve()
    if cache_project is None:return [old/'build'/n for n in ('downloads','sstate-cache')],'INDEPENDENT_COPY'
    own=Path(cache_project).resolve()
    if own==candidate or own.parent!=old.parent.parent or not own.name.startswith('codex_linux_backend_hunet_v5_'):
        raise ValueError('CACHE_NOT_OWN_HW4_FAMILY')
    integration=json.loads((own/'integration.json').read_text())
    if integration.get('scope')!='HW4_HUNET_ISOLATED_IMAGE_BUILD' or integration.get('manifest',{}).get('profile')!=4:
        raise ValueError('CACHE_NOT_OWN_HW4_PROFILE')
    caches=[own/'env_backend/build'/n for n in ('downloads','sstate-cache')]
    if any(not p.is_dir() or p.resolve().parent!=own/'env_backend/build' for p in caches):
        raise ValueError('CACHE_NOT_OWN_NATIVE_DIRECTORIES')
    return caches,'OWN_HW4_SHARED'


def offline_settings():
    return '\n# Independent HW4 candidate: bounded offline build\nBB_NO_NETWORK = "1"\nSSTATE_MIRRORS = ""\nBB_NUMBER_THREADS = "2"\nPARALLEL_MAKE = "-j 2"\nBB_DISKMON_DIRS = "STOPTASKS,${TMPDIR},1500M,100K"\n'


def prepare_image(base_dir, candidate_dir, release_dir, xsa, cache_project=None):
    if (Path(release_dir)/'release-hybrid.json').is_file():
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'hunet_hybrid'))
        from hybrid_image import prepare_hybrid
        return prepare_hybrid(base_dir,candidate_dir,release_dir,xsa,cache_project)
    old, base = validate_paths(base_dir, candidate_dir)
    release_dir=Path(release_dir).resolve();xsa=Path(xsa).resolve();release=checked_release(release_dir)
    if sha(xsa)!=release['xsa_sha256']:
        raise ValueError('FROZEN_HW4_XSA_REQUIRED')
    protected=protected_files(old)
    caches,cache_mode=cache_directories(old,base,cache_project)
    if not all(p.is_dir() for p in caches):raise ValueError('OFFLINE_PARENT_CACHE_REQUIRED')
    cache_bytes=sum(int(subprocess.check_output(['du','-sb',str(p)],universal_newlines=True).split()[0]) for p in caches)
    if shutil.disk_usage(old).free<(cache_bytes if cache_mode=='INDEPENDENT_COPY' else 0)+8*1024**3:
        raise ValueError('INSUFFICIENT_NATIVE_DISK_FOR_INDEPENDENT_CACHE')
    native_output(base);write_json(base/'protected-original.json',protected)
    new=base/'env_backend'
    def ignore(path,names):
        return [n for n in names if n=='.git' or n.endswith('.log') or
                (Path(path)==old and n in ('build','images'))]
    shutil.copytree(str(old),str(new),ignore=ignore)
    (new/'build').mkdir()
    source=source_snapshot(release_dir,base/'sealed-source')
    license_md5=hashlib.md5((source/'vendor/vortex/LICENSE').read_bytes()).hexdigest()
    recipe=recipes(release['source_commit'],license_md5)
    meta=new/'project-spec/meta-user'
    def write(name,text):
        path=meta/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(text,encoding='utf-8')
    write('recipes-kernel/linux/linux-xlnx_%.bbappend',recipe['kernel'])
    write('recipes-kernel/linux/files/vxl-built-in.cfg','CONFIG_VXL_LINUX=y\nCONFIG_ZONE_DMA32=y\nCONFIG_CRYPTO_SHA256=y\n')
    write('recipes-apps/vxl-runtime/vxl-runtime.bb',recipe['apps'])
    for name in ('recipes-kernel/linux/files','recipes-apps/vxl-runtime/files'):
        folder=meta/name;folder.mkdir(exist_ok=True,parents=True)
        shutil.copy2(str(release_dir/'source.tar.gz'),str(folder/'vxl-source.tar.gz'))
        shutil.copy2(str(release_dir/'build/vxl_hunet_source.h'),str(folder/'vxl_hunet_source.h'))
    release_archive(release_dir,meta/'recipes-apps/vxl-runtime/files/hunet-release.tar.gz',release)
    build_id='HW4-HUNET-V5-LINUX-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
    manifest=dict(scope='HW4_HUNET_V5_LINUX_CANDIDATE_V1',profile=4,build_id=build_id,
        sd_variant=SD_VARIANT,**{k:release[k] for k in ('source_commit','source_digest','bit_sha256','xsa_sha256','binding_sha256')})
    public_key=old/'project-spec/meta-user/recipes-apps/codex-firstboot/files/authorized_keys'
    if not public_key.is_file() or not public_key.read_bytes().strip():raise ValueError('TRUSTED_PUBLIC_KEY_REQUIRED')
    write('recipes-apps/codex-firstboot/codex-firstboot.bb',recipe['firstboot'])
    write('recipes-apps/codex-firstboot/files/codex-build-id',build_id+'\n')
    write_json(meta/'recipes-apps/codex-firstboot/files/codex-hunet-manifest.json',manifest)
    write('recipes-apps/codex-firstboot/files/codex-direct-network',
        '#!/bin/sh\ncase "$1" in\nstart)\n ip link set dev eth0 up || exit $?\n ip addr replace 192.168.13.10/24 dev eth0\n ;;\nstop) : ;;\nesac\n')
    write('recipes-core/init-ifupdown/files/interfaces',
        'auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet static\n    address 192.168.13.10\n    netmask 255.255.255.0\n')
    write('recipes-bsp/device-tree/files/system-user.dtsi',dt_source())
    for name in ('boot.cmd.default','boot.cmd.default.initrd'):
        write('recipes-bsp/u-boot/u-boot-zynq-scr/'+name,BOOT_COMMAND)
    config=new/'project-spec/configs/config';text=config.read_text()
    text,n=re.subn(r'^CONFIG_SUBSYSTEM_HOSTNAME=.*$', 'CONFIG_SUBSYSTEM_HOSTNAME="codex-hw4-hunet-v5"',text,flags=re.M)
    if n!=1:raise ValueError('PARENT_HOSTNAME_CONFIG')
    config.write_text(text,encoding='utf-8')
    bsp=meta/'conf/petalinuxbsp.conf'
    with bsp.open('a') as stream:
        stream.write(offline_settings())
    driver_make=(source/'driver/Makefile').read_text().replace('obj-m += vxl_linux.o','obj-$(CONFIG_VXL_LINUX) += vxl_linux.o')
    driver_make=re.sub(r'^ccflags-y \+= -I.* -Werror$', 'ccflags-y += -I$(srctree)/drivers/misc/vxl/include -Werror',driver_make,flags=re.M)
    integration=dict(scope='HW4_HUNET_ISOLATED_IMAGE_BUILD',manifest=manifest,source_project=str(old),
        derived_project=str(new),release_dir=str(release_dir),trusted_public_key=str(public_key),
        kernel_makefile=driver_make,cache_bytes=cache_bytes,cache_mode=cache_mode,cache_project=None if cache_project is None else str(Path(cache_project).resolve()),board='NOT_RUN',sd='NOT_TOUCHED',h1='NOT_RUN')
    write_json(base/'integration.json',integration)
    for source_cache in caches:
        if cache_mode=='INDEPENDENT_COPY':
            print('COPY_INDEPENDENT_CACHE',source_cache,flush=True)
            subprocess.check_call(['cp','-a','--reflink=auto',str(source_cache),str(new/'build'/source_cache.name)])
        else:
            print('REUSE_OWN_HW4_CACHE',source_cache,flush=True)
            (new/'build'/source_cache.name).symlink_to(source_cache,target_is_directory=True)
    for path,want in protected.items():
        if sha(Path(path))!=want:raise ValueError('PROTECTED_PARENT_CHANGED:'+path)
    print('HW4_HUNET_PROJECT_PREPARED',new,build_id,flush=True)
    return integration


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for option in ('base','candidate','release','xsa'):parser.add_argument('--'+option,type=Path,required=True)
    parser.add_argument('--cache-project',type=Path)
    args=parser.parse_args()
    prepare_image(args.base,args.candidate,args.release,args.xsa,args.cache_project)
