SUMMARY = "Source-bound lightweight dual application controller and DP viewer"
LICENSE = "CLOSED"
SRC_URI = "file://demo-runtime.tar.gz;subdir=demo-runtime"
S = "${WORKDIR}/demo-runtime"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/bin/vxl_demo ${D}${bindir}/
    install -m 0755 ${S}/bin/vxl_dp_viewer ${D}${bindir}/
    install -m 0755 ${S}/bin/demo-viewer-init ${D}${bindir}/
}
RDEPENDS_${PN} += "demo-data libstdc++ libgcc"
INSANE_SKIP_${PN} += "ldflags already-stripped"
