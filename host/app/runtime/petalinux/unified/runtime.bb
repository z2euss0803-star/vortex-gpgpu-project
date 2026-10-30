SUMMARY = "Identity-bound common GPU owner and control client"
LICENSE = "CLOSED"
SRC_URI = "file://unified-runtime.tar.gz;subdir=unified-runtime"
S = "${WORKDIR}/unified-runtime"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
DEPENDS += "libdrm"
RDEPENDS_${PN} += "libstdc++ libgcc libdrm python3-core python3-io python3-json python3-misc"
INSANE_SKIP_${PN} += "ldflags already-stripped"
inherit update-rc.d
INITSCRIPT_NAME = "vxgpud"
INITSCRIPT_PARAMS = "start 95 2 3 4 5 . stop 10 0 1 6 ."
do_install() {
    cp -a ${S}/. ${D}/
}
FILES_${PN} += "/etc/vxgpud /etc/default/vxgpud /etc/init.d/vxgpud /usr/share/vxgpu/payload-manifest.json"
