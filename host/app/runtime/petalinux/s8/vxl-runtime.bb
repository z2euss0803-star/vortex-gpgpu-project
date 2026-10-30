SUMMARY = "Exact controlled S8 userspace and data"
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
