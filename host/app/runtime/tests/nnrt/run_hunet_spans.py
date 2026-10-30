"""Build and run actual entry C++ with only GPU instruction adapters on A.

The generated translation unit preserves the scalar callbacks, launch selection,
pointer binding, validation and return path verbatim. A transparent store observer
also counts output ownership. No numerical expression or output value is mocked.
Every invocation uses a fresh output directory and retains commands/exit/logs.
"""
import argparse
import hashlib
import json
import pathlib
import platform
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--red", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--related", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT / "device/vxnn/hunet_entry.cpp"
    body = source.read_text()
    adapters = {
        "#include <vx_spawn.h>\n": "",
        "#include <vx_tensor.h>\n": "",
        'void fence(){__asm__ volatile("fence iorw, iorw":::"memory");}': "void fence(){}",
        "volatile uint32_t* error_word(uint32_t core){return reinterpret_cast<volatile uint32_t*>(uintptr_t(0x10002000u+core*256+64));}": "volatile uint32_t* error_word(uint32_t core){return r4_errors+core;}",
        'bool rounding_ok(){uint32_t f;__asm__ volatile("csrr %0, fcsr":"=r"(f));return (f&0xe0u)==0;}': "bool rounding_ok(){return std::fegetround()==FE_TONEAREST;}",
        "int main(){": "int r4_hunet_entry_main(){",
    }
    for old, new in adapters.items():
        assert body.count(old) == 1, "GPU adapter anchor changed: " + old
        body = body.replace(old, new)
    assert "__asm__" not in body
    dispatch_header = ROOT / "device/vxnn/hunet_scalar_dispatch.h"
    if dispatch_header.exists():
        dispatch = dispatch_header.read_text()
        store = "y[t]=value;if(!isfinite(value))finite=false;"
        assert dispatch.count(store) == 1, "transparent store observer anchor changed"
        dispatch = dispatch.replace(store, "r4_track_write(y,t);" + store)
        (out / "r4_hunet_dispatch_host.inc").write_text(dispatch)
        assert body.count('#include "hunet_scalar_dispatch.h"') == 1
        body = body.replace('#include "hunet_scalar_dispatch.h"', '#include "r4_hunet_dispatch_host.inc"')
    (out / "r4_hunet_entry_host.inc").write_text(body)
    flags = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-fno-fast-math", "-ffp-contract=off", "-frounding-math"]
    if args.sanitize:
        flags += ["-fsanitize=undefined", "-fno-sanitize-recover=undefined"]
    command = ["g++"] + flags + ["-Iinclude", "-I.", "-Idevice/vxnn", "-Ivendor/hunet/v5/include", "-I" + str(out), "tests/nnrt/test_hunet_spans.cpp", "device/vxnn/hunet_wire.cpp", "-lm", "-o", str(out / "test_hunet_spans")]
    files = [source, ROOT / "device/vxnn/hunet_wire.cpp", ROOT / "device/vxnn/hunet_wire.h", ROOT / "device/vxnn/hunet_ops.h", ROOT / "device/vxnn/hunet_scalar_math.h", ROOT / "tests/nnrt/test_hunet_spans.cpp", pathlib.Path(__file__).resolve(), ROOT / "vendor/hunet/v5/include/hunet_ops_baseline.h", ROOT / "vendor/hunet/v5/include/hunet_scalar_spans.h", ROOT / "vendor/hunet/v5/include/hunet_ordered_reductions.h", ROOT / "generated/hunet_fused/jobs.bin"]
    files += sorted((ROOT / "device/vxnn").glob("hunet_scalar_dispatch*.h"))
    files += [ROOT / p for p in ("vendor/hunet/v5/include/hunet_math.h", "vendor/hunet/v5/include/hunet_job.h", "device/hunet_fused_ops.h", "device/vxnn/ranges.h", "include/vxl/hunet_fused_wire.h")]
    receipt = {"host": platform.node(), "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "root": str(ROOT), "mode": "red" if args.red else "green", "sanitize": args.sanitize, "gpu": "NOT_RUN", "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}, "runs": []}

    def run(label, cmd):
        started = time.monotonic()
        with (out / (label + ".log")).open("wb") as log:
            result = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        record = {"label": label, "command": cmd, "natural_exit": result.returncode, "wall_seconds": time.monotonic() - started}
        receipt["runs"].append(record)
        (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(record), flush=True)
        print((out / (label + ".log")).read_text(), end="", flush=True)
        return result.returncode

    assert run("compiler", ["g++", "--version"]) == 0
    assert run("build", command) == 0
    for mode in ("equivalence", "faults", "dispatch"):
        rc = run(mode, [str(out / "test_hunet_spans"), mode])
        expected = 69 if args.red and mode == "dispatch" else 0
        assert rc == expected, (mode, rc, expected)
    if args.related:
        command = ["make", "-f", "Makefile.unified", "-j1", "OUT=" + str(out / "related"), "test-unified-hunet-layout", "test-unified-hunet-scalar", "test-unified-hunet-wire"]
        if args.sanitize:
            command += ["SANITIZE=undefined"]
        assert run("related", command) == 0
    receipt["test_binary_sha256"] = hashlib.sha256((out / "test_hunet_spans").read_bytes()).hexdigest()
    receipt["result"] = "EXPECTED_DISPATCH_RED" if args.red else "HOST_COMPONENT_PASS_NOT_GPU"
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(receipt["result"], flush=True)


if __name__ == "__main__":
    main()
