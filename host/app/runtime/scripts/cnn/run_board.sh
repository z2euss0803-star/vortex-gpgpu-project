#!/bin/sh
# Future live use only, after the operator approves this exact test scope.
# The environment gate is an execution guard, not evidence of user consent.
set -eu
if [ "${CNN_BOARD_EXECUTION_APPROVED:-}" != COPY-VECADD-CNN-96-JOBS ]; then
    echo NO_BOARD_AUTHORIZATION >&2
    exit 2
fi
[ "$#" -eq 6 ] || { echo 'Usage: run_board.sh BUILD COMMIT BOOT_UUID MANIFEST_SHA TOOL_SHA RUN_ID' >&2; exit 2; }
build=$1 commit=$2 boot=$3 manifest_sha=$4 tool_sha=$5 run_id=$6
case "$run_id" in ''|*[!a-zA-Z0-9_-]*) echo INVALID_ARGUMENTS >&2; exit 2;; esac
printf '%s\n' "$build" | grep -Eq '^H0-LINUX-CNN-I32-V1-[0-9]{8}T[0-9]{6}Z$' || { echo INVALID_ARGUMENTS >&2; exit 2; }
printf '%s\n' "$commit" | grep -Eq '^[0-9a-f]{40}$' || { echo INVALID_ARGUMENTS >&2; exit 2; }
printf '%s\n' "$boot" | grep -Eq '^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$' || { echo INVALID_ARGUMENTS >&2; exit 2; }
for digest in "$manifest_sha" "$tool_sha"; do
    printf '%s\n' "$digest" | grep -Eq '^[0-9a-f]{64}$' || { echo INVALID_ARGUMENTS >&2; exit 2; }
done
# All conditions below are checked before the first GPU/control test.
[ "$(uname -m)" = aarch64 ] || { echo WRONG_ARCH >&2; exit 1; }
[ "$(uname -r)" = 5.4.0-xilinx-v2020.1 ] || { echo WRONG_KERNEL >&2; exit 1; }
[ "$(cat /etc/codex-build-id)" = "$build" ] || { echo WRONG_BUILD >&2; exit 1; }
check_boot() { [ "$(cat /proc/sys/kernel/random/boot_id)" = "$boot" ] || { echo BOOT_CHANGED >&2; exit 1; }; }
check_boot
[ "$(sha256sum /etc/codex-cnn-manifest.json | cut -d ' ' -f 1)" = "$manifest_sha" ] || { echo WRONG_MANIFEST >&2; exit 1; }
[ "$(sha256sum /usr/bin/vxl_cnn | cut -d ' ' -f 1)" = "$tool_sha" ] || { echo WRONG_CNN_TOOL >&2; exit 1; }
for name in LD_PRELOAD LD_AUDIT VXL_TRANSPORT VORTEX_DRIVER; do
    case "$name" in
        LD_PRELOAD) value=${LD_PRELOAD:-};; LD_AUDIT) value=${LD_AUDIT:-};;
        VXL_TRANSPORT) value=${VXL_TRANSPORT:-};; VORTEX_DRIVER) value=${VORTEX_DRIVER:-};;
    esac
    [ -z "$value" ] || { echo REJECTED_ENVIRONMENT >&2; exit 1; }
done
# Structured installed/compiled identity is checked before any device test.
# Older tools without this mode fail closed and require a matching new image.
/usr/bin/vxl_cnn --check-identity "$commit" "$build" "$boot"
out=/tmp/cnn-board-$run_id
mkdir "$out"
finalize() {
    original_rc=$?
    trap - 0
    set +e
    printf '%s\n' "$original_rc" > "$out/runner.exit"
    dmesg > "$out/dmesg-after.txt" 2> "$out/dmesg-after.stderr"
    collect_rc=$?
    printf 'dmesg_exit=%s\n' "$collect_rc" >> "$out/finalize.log"
    if [ "$collect_rc" -ne 0 ] && [ "$original_rc" -eq 0 ]; then original_rc=1; fi
    cat /proc/sys/kernel/random/boot_id > "$out/boot-id-after.txt" 2> "$out/boot-id-after.stderr"
    collect_rc=$?
    printf 'boot_id_read_exit=%s\n' "$collect_rc" >> "$out/finalize.log"
    if [ "$collect_rc" -ne 0 ] && [ "$original_rc" -eq 0 ]; then original_rc=1; fi
    if [ "$(cat "$out/boot-id-after.txt")" != "$boot" ]; then
        echo boot_changed=1 >> "$out/finalize.log"
        [ "$original_rc" -ne 0 ] || original_rc=1
    fi
    printf '%s\n' "$original_rc" > "$out/runner.exit"
    # Freeze the collection log before hashing it. Keep hash stderr outside the
    # inventory set because the hash operation itself writes that stream.
    (cd "$out" && find . -type f ! -name SHA256SUMS ! -name hash-collection.stderr | while IFS= read -r file; do sha256sum "$file" || exit 1; done) > "$out/SHA256SUMS" 2> "$out/hash-collection.stderr"
    hash_rc=$?
    if [ "$hash_rc" -ne 0 ] || [ -s "$out/hash-collection.stderr" ]; then
        echo HASH_COLLECTION_FAILED >&2
        [ "$original_rc" -ne 0 ] || original_rc=1
        printf '%s\n' "$original_rc" > "$out/runner.exit"
        echo HASH_COLLECTION_FAILED > "$out/SHA256SUMS.INCOMPLETE"
    fi
    exit "$original_rc"
}
trap finalize 0
export LD_LIBRARY_PATH=/usr/lib
printf 'BUILD=%s\nSOURCE=%s\nBOOT=%s\nSCOPE=COPY-VECADD-CNN-96-JOBS\n' "$build" "$commit" "$boot" > "$out/expected-identity.txt"
cat /proc/cmdline > "$out/cmdline.txt"
uname -a > "$out/uname.txt"
dmesg > "$out/dmesg-before.txt"
cp /etc/codex-cnn-manifest.json "$out/installed-manifest.json"
run_test() {
    name=$1
    shift
    check_boot
    rc=0
    "$@" > "$out/$name.stdout" 2> "$out/$name.stderr" || rc=$?
    printf '%s\n' "$rc" > "$out/$name.exit"
    check_boot
    if [ "$rc" -ne 0 ]; then
        echo "STOPPED_AT=$name EXIT=$rc EVIDENCE=$out" >&2
        exit "$rc"
    fi
}
run_test info /usr/bin/vxl_info
run_test copy /usr/bin/vxl_copy_test
run_test vecadd /usr/bin/vxl_vecadd
run_test cnn /usr/bin/vxl_cnn --evidence-dir "$out/cnn"
run_test final-info /usr/bin/vxl_info
check_boot
echo "CNN_BOARD_COMMANDS_EXIT0_REQUIRES_INDEPENDENT_AUDIT EVIDENCE=$out"
