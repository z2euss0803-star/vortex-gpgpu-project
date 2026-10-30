FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += " file://vxl-source.tar.gz;subdir=vxl-source file://vxl_s8_release.h file://vxl-built-in.cfg"
do_patch_append() {
    install -d ${S}/drivers/misc/vxl
    cp -a ${WORKDIR}/vxl-source/driver ${WORKDIR}/vxl-source/core ${WORKDIR}/vxl-source/include ${WORKDIR}/vxl-source/generated ${S}/drivers/misc/vxl/
    install -m 0644 ${WORKDIR}/vxl_s8_release.h ${S}/drivers/misc/vxl/driver/
    sed -i 's/^obj-m += vxl_linux.o/obj-$(CONFIG_VXL_LINUX) += vxl_linux.o/' ${S}/drivers/misc/vxl/driver/Makefile
    sed -i 's|^ccflags-y += -I.* -Werror|ccflags-y += -I$(srctree)/drivers/misc/vxl/include -Werror|' ${S}/drivers/misc/vxl/driver/Makefile
    grep -q 'vxl/driver/' ${S}/drivers/misc/Makefile || printf '\nobj-$(CONFIG_VXL_LINUX) += vxl/driver/\n' >> ${S}/drivers/misc/Makefile
    grep -q 'vxl/driver/Kconfig' ${S}/drivers/misc/Kconfig || printf '\nsource "drivers/misc/vxl/driver/Kconfig"\n' >> ${S}/drivers/misc/Kconfig
}
