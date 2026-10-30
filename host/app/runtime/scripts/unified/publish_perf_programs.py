"""Publish independently built R4 programs without editing old families."""
import argparse
import json
import pathlib
import shutil
from scripts.unified.program_identity import sha


def publish(root, hunet_build, mosaic_build):
    root = pathlib.Path(root).resolve()
    pairs = [("hunet-fp16-v1", "hunet-fp16-v2", "span32-v2", pathlib.Path(hunet_build)),
             ("mosaic-int16-v2", "mosaic-int16-v3", "pack32-ro64-v3", pathlib.Path(mosaic_build))]
    prepared = []
    legacy_pins = {}
    for old_name, name, variant, build in pairs:
        legacy = root / "data/nnrt/programs" / old_name
        out = root / "data/nnrt/programs" / name
        if out.exists():
            raise ValueError("NEW_PROGRAM_DESTINATION_MUST_NOT_EXIST " + name)
        old = json.loads((legacy / "family.json").read_bytes())
        receipt = json.loads((build / "receipt.json").read_bytes())
        if (receipt.get("natural_exit") != 0 or receipt.get("target_executed") is not False
                or receipt.get("gpu_raw_cycles", 0) is not None or not receipt.get("stack")
                or not receipt.get("object_ownership") or receipt["flat"]["bytes"] != 65536
                or sha(build / "code.bin") != receipt["flat"]["sha256"]
                or sha(build / "kernel.elf") != receipt["elf"]["sha256"]
                or sha(build / "code.bin") == old["code_sha256"]):
            raise ValueError("ACTUAL_NEW_COMPLETE_RV32_BUILD_REQUIRED " + name)
        if name.startswith("hunet"):
            if receipt.get("dispatch_variant") != "span32-v2" or len(receipt.get("audited_callback_names", [])) != 12:
                raise ValueError("HUNET_ALL_CALLBACKS_REQUIRED")
        elif receipt.get("variant") != "ro64-v2" or sha(build / "rodata.bin") != old["rodata_sha256"]:
            raise ValueError("UNCHANGED_RO64_VALIDATOR_AND_TABLES_REQUIRED")
        for filename, key in [("code.bin", "code_sha256"), ("args-schema.bin", "schema_sha256")]:
            if sha(legacy / filename) != old[key]:
                raise ValueError("LEGACY_ARTIFACT_CHANGED " + old_name)
        for path in legacy.iterdir():
            if path.is_file():
                legacy_pins[path] = sha(path)
        sources = {}
        for name_path, digest in receipt["sources"].items():
            canonical = (root / name_path).resolve().relative_to(root).as_posix()
            if canonical in sources and sources[canonical] != digest:
                raise ValueError("CONFLICTING_ACTUAL_BUILD_SOURCE " + canonical)
            sources[canonical] = digest
        for name_path, digest in receipt["dependencies"].items():
            path = pathlib.Path(name_path).resolve()
            try:
                relative = path.relative_to(root).as_posix()
            except ValueError:
                continue
            if relative.startswith("build/"):
                continue
            if relative in sources and sources[relative] != digest:
                raise ValueError("CONFLICTING_ACTUAL_BUILD_SOURCE " + relative)
            sources[relative] = digest
        for row in old["sources"]:
            path, digest = row["path"], row["sha256"]
            if path not in sources:
                if sha(root / path) != digest:
                    raise ValueError("CHANGED_SOURCE_NOT_IN_ACTUAL_BUILD " + path)
                sources[path] = digest
        for path, digest in sources.items():
            if sha(root / path) != digest:
                raise ValueError("ACTUAL_BUILD_SOURCE_DRIFT " + path)
        family = dict(old)
        for key in ("evidence_archive", "evidence_sha256"):
            family.pop(key, None)
        family.update(scope="R4_ACTUAL_RV32_STATIC_NOT_GPU_EXECUTED", variant=variant,
                      code_sha256=receipt["flat"]["sha256"], program_receipt_sha256=sha(build / "receipt.json"),
                      sources=[dict(path=p, sha256=d) for p, d in sorted(sources.items())],
                      legacy=dict(directory="data/nnrt/programs/" + old_name, family_sha256=sha(legacy / "family.json"), code_sha256=old["code_sha256"]),
                      gpu_raw_cycles=None)
        prepared.append((out, legacy, build, family))
    for out, legacy, build, family in prepared:
        out.mkdir()
        for source, filename in [(build / "code.bin", "code.bin"), (build / "kernel.elf", "kernel.elf"),
                                 (build / "receipt.json", "rv32-build.json"), (legacy / "args-schema.bin", "args-schema.bin")]:
            shutil.copyfile(source, out / filename)
        if family.get("rodata_sha256"):
            shutil.copyfile(build / "rodata.bin", out / "rodata.bin")
        (out / "family.json").write_text(json.dumps(family, indent=2) + "\n", encoding="utf8")
    hunet, mosaic = prepared[0][3], prepared[1][3]
    hrows = {r["path"]: r["sha256"] for r in hunet["sources"]}
    mrows = {r["path"]: r["sha256"] for r in mosaic["sources"]}
    for filename, pins in [("hunet_span_program.hpp", dict(hunet_span_code_sha256=hunet["code_sha256"], hunet_span_dispatch_sha256=hrows["device/vxnn/hunet_scalar_dispatch.h"])),
                           ("mosaic_ro64_program.hpp", dict(mosaic_ro64_code_sha256=mosaic["code_sha256"], mosaic_ro64_validator_sha256=mrows["device/vxnn/mosaic_wire_ro64.cpp"], mosaic_pack32_source_sha256=mrows["device/mosaic_s8/quant_pack.cpp"]))]:
        text = "#pragma once\n// Actual R4 static build pins. No GPU execution claim.\nnamespace vxnn {\n"
        text += "".join('inline constexpr const char*' + key + '=\"' + value + '\";\n' for key, value in pins.items())
        (root / "include/vxnn" / filename).write_bytes((text + "}\n").encode())
    if any(sha(path) != digest for path, digest in legacy_pins.items()):
        raise ValueError("LEGACY_FAMILY_MUST_REMAIN_BYTE_IDENTICAL")
    from scripts.unified.hunet_program import selected_directory as select_hunet
    from scripts.unified.mosaic_program import selected_directory as select_mosaic
    select_hunet(root)
    select_mosaic(root)
    return dict(scope="R4_INDEPENDENT_ACTUAL_PROGRAM_PUBLICATION_NOT_GPU_PASS", legacy_files_verified=len(legacy_pins),
                families=[dict(directory=str(out), family_sha256=sha(out / "family.json"), code_sha256=family["code_sha256"], program_receipt_sha256=family["program_receipt_sha256"]) for out, _, _, family in prepared], gpu_raw_cycles=None)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True)
    p.add_argument("--hunet-build", required=True)
    p.add_argument("--mosaic-build", required=True)
    a = p.parse_args()
    print(json.dumps(publish(a.root, a.hunet_build, a.mosaic_build), indent=2))
