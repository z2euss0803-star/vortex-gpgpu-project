"""Controlled publication of R13 static RV32 builds without historical writes."""
import json
import pathlib
import shutil
import tempfile
from scripts.unified.program_identity import compiled_sources, sha, R13_LEGACY


def prepare(root, build, name, entry):
    root = pathlib.Path(root).resolve()
    build = pathlib.Path(build).resolve()
    old_name, old_code = R13_LEGACY[name]
    olddir = root / "data/nnrt/programs" / old_name
    old = json.loads((olddir / "family.json").read_bytes())
    receipt = json.loads((build / "receipt.json").read_bytes())
    if (type(receipt.get("natural_exit")) is not int or receipt["natural_exit"] != 0
            or receipt.get("target_executed") is not False or receipt.get("gpu_raw_cycles", 0) is not None
            or not receipt.get("object_ownership") or not receipt.get("stack")
            or type(receipt["stack"].get("bytes")) is not int or not 0 < receipt["stack"]["bytes"] < 8128
            or not receipt.get("commands")
            or any(type(c.get("natural_exit")) is not int or c["natural_exit"] != 0 for c in receipt["commands"])):
        raise ValueError("ACTUAL_COMPLETE_STATIC_RV32_BUILD_REQUIRED " + name)
    if (receipt.get("flat") != dict(bytes=65536, sha256=sha(build / "code.bin"))
            or (build / "code.bin").stat().st_size != 65536
            or receipt.get("elf") != dict(bytes=(build / "kernel.elf").stat().st_size, sha256=sha(build / "kernel.elf"))
            or old.get("code_sha256") != old_code or sha(olddir / "code.bin") != old_code
            or receipt["flat"]["sha256"] == old_code):
        raise ValueError("ACTUAL_NEW_CODE_AND_FROZEN_PREDECESSOR_REQUIRED " + name)
    sources = compiled_sources(receipt, entry)
    for path, digest in sources.items():
        if sha(root / path) != digest:
            raise ValueError("ACTUAL_COMPILED_SOURCE_DRIFT " + path)
    family = {k: v for k, v in old.items() if k not in ("evidence_archive", "evidence_sha256")}
    family.update(scope="R13_ACTUAL_RV32_STATIC_NOT_GPU_EXECUTED", gpu_raw_cycles=None,
                  code_sha256=receipt["flat"]["sha256"], program_receipt_sha256=sha(build / "receipt.json"),
                  legacy=dict(directory="data/nnrt/programs/" + old_name,
                              family_sha256=sha(olddir / "family.json"), code_sha256=old_code))
    return root, build, olddir, old, receipt, sources, family


def finish(root, build, olddir, name, family, sources, schema, header, pin_values, selector, script):
    out = root / "data/nnrt/programs" / name
    header_path = root / header
    if out.exists() or header_path.exists():
        raise ValueError("NEW_PROGRAM_AND_PIN_DESTINATIONS_MUST_NOT_EXIST " + name)
    family["sources"] = [dict(path=p, sha256=d) for p, d in sorted(sources.items())]
    family["publication_source"] = dict(path=script, sha256=sha(root / script))
    legacy = {path: sha(path) for path in (root / "data/nnrt/programs").rglob("*") if path.is_file()}
    legacy.update({path: sha(path) for path in (root / "include/vxnn").glob("*program.hpp") if path.is_file()})
    text = "#pragma once\n// Actual R13 RV32 static build. GPU execution and performance unverified.\nnamespace vxnn {\n"
    text += "".join('inline constexpr const char*' + key + '=\"' + value + '\";\n' for key, value in pin_values.items())
    prefix = "hunet_tile2" if name.startswith("hunet") else "mosaic_reuse_v8"
    text += 'inline constexpr const char*' + prefix + '_variant=\"' + family["variant"] + '\";\n'
    text += 'inline constexpr const char*' + prefix + '_directory=\"' + name + '\";\n}\n'
    # The overlay copies only the actual source closure and this predecessor.
    # Validation runs before a new family or pin can become visible in root.
    staging_parent = root / "build"
    staging_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=staging_parent, prefix="r13-program-publication-") as tmp:
        staged_root = pathlib.Path(tmp).resolve()
        if not staged_root.is_relative_to(staging_parent.resolve()):
            raise ValueError("PUBLICATION_STAGING_MUST_STAY_IN_WORKSPACE")
        copied_sources = set(sources)
        if family.get("schema_source"):
            copied_sources.add(family["schema_source"]["path"])
        for path in copied_sources:
            target = staged_root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / path, target)
        staged_old = staged_root / "data/nnrt/programs" / olddir.name
        staged_old.mkdir(parents=True)
        for filename in ("family.json", "code.bin"):
            shutil.copyfile(olddir / filename, staged_old / filename)
        staged = staged_root / "data/nnrt/programs" / name
        staged.mkdir()
        for source, filename in ((build / "code.bin", "code.bin"), (build / "kernel.elf", "kernel.elf"),
                                 (build / "receipt.json", "rv32-build.json")):
            shutil.copyfile(source, staged / filename)
        if family.get("rodata_sha256"):
            shutil.copyfile(build / "rodata.bin", staged / "rodata.bin")
        (staged / "args-schema.bin").write_bytes(schema)
        (staged / "family.json").write_text(json.dumps(family, indent=2) + "\n", encoding="utf8")
        staged_header = staged_root / header
        staged_header.parent.mkdir(parents=True, exist_ok=True)
        staged_header.write_bytes(text.encode())
        selector(staged_root)
        if any(sha(path) != digest for path, digest in legacy.items()):
            raise ValueError("OLD_PUBLISHED_PROGRAM_OR_PIN_CHANGED")
        shutil.copytree(staged, out)
        with header_path.open("xb") as target:
            target.write(text.encode())
    selector(root)
    if any(sha(path) != digest for path, digest in legacy.items()):
        raise ValueError("OLD_PUBLISHED_PROGRAM_OR_PIN_CHANGED")
    return dict(scope=family["scope"], natural_exit=0, directory=str(out), pin_header=header,
                family_sha256=sha(out / "family.json"), code_sha256=family["code_sha256"],
                program_receipt_sha256=family["program_receipt_sha256"], compiled_sources=len(sources),
                old_published_files_unchanged=len(legacy), gpu_raw_cycles=None)
