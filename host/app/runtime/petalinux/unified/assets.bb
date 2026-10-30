SUMMARY = "Frozen provider-specific model, program, visual and numeric evidence assets"
LICENSE = "CLOSED"
SRC_URI = "file://unified-assets.tar.gz;subdir=unified-assets"
S = "${WORKDIR}/unified-assets"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
do_install() {
    cp -a ${S}/. ${D}/
}
FILES_${PN} += "/lib/firmware/vxl /usr/share/vxgpu"
