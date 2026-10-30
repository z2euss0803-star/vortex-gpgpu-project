SUMMARY = "Frozen native INT16 Mosaic and FP32 HUNet data and identity bindings"
LICENSE = "CLOSED"
SRC_URI = "file://demo-data.tar.gz;subdir=demo-data"
S = "${WORKDIR}/demo-data"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
do_install() {
    install -d -m 0755 ${D}${datadir}/codex-demo
    cp -R --no-preserve=ownership ${S}/. ${D}${datadir}/codex-demo/
    find ${D}${datadir}/codex-demo -type d -exec chmod 0755 {} \;
    find ${D}${datadir}/codex-demo -type f -exec chmod 0644 {} \;
}
FILES_${PN} += "${datadir}/codex-demo"
INSANE_SKIP_${PN} += "ldflags already-stripped"
