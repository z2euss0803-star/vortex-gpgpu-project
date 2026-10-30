SUMMARY = "HW4 candidate identity and trusted public-key access"
LICENSE = "CLOSED"
SRC_URI = "file://codex-direct-network file://authorized_keys file://codex-build-id file://codex-s8-manifest.json"
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
    install -m 0644 ${WORKDIR}/codex-s8-manifest.json ${D}${sysconfdir}/codex-s8-manifest.json
}
FILES_${PN} += "/home/root/.ssh /home/root/.ssh/authorized_keys"
