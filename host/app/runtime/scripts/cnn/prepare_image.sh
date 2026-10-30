#!/bin/bash
set -euo pipefail
test "$#" -eq 5 || { echo 'Usage: prepare_image.sh OLD_PROJECT NEW_BASE SNAPSHOT SOURCE_ARCHIVE SHA256' >&2; exit 2; }
script_dir=$(cd "$(dirname "$0")" && pwd)
python3 "$script_dir/prepare_image.py" --old "$1" --base "$2" --snapshot "$3" --payload "$4" --sha256 "$5"
source /home/z2euss/petalinux/2020.1/settings.sh /home/z2euss/petalinux/2020.1
export LC_ALL=en_US.UTF-8 TERM=dumb
petalinux-config -p "$2/env_backend" --silentconfig > "$2/config.log" 2>&1
base=$(readlink -f "$2")
cat > "$base/run_build.sh" <<'SH'
#!/bin/bash
set -eo pipefail
base=$(cd "$(dirname "$0")" && pwd)
source /home/z2euss/petalinux/2020.1/settings.sh /home/z2euss/petalinux/2020.1
export LC_ALL=en_US.UTF-8 TERM=dumb
rc=0
petalinux-build -p "$base/env_backend" > "$base/build.log" 2>&1 || rc=$?
printf '%s\n' "$rc" > "$base/build.exit"
date -u +%Y-%m-%dT%H:%M:%SZ > "$base/build.finished"
exit "$rc"
SH
chmod 700 "$base/run_build.sh"
nohup "$base/run_build.sh" > "$base/launcher.log" 2>&1 < /dev/null &
printf '%s\n' "$!" > "$base/build.pid"
echo CNN_IMAGE_BUILD_STARTED_PID=$(cat "$base/build.pid")
