"""Fresh admission of an actually built, independently versioned GPU family."""
import hashlib
import json
import pathlib
import posixpath
import re


# Publication identities are deliberately narrower than a prefix or a free-form
# receipt label. R13 v8 uses the original reuse-v7 recipe without rewriting it.
PROGRAM_ENTRIES = {
    ("hunet-fp16-v2", "span32-v2"): "device/vxnn/hunet_entry.cpp",
    ("hunet-fp16-v3", "span32-tile2-v3"): "device/vxnn/hunet_entry.cpp",
    ("mosaic-int16-v3", "pack32-ro64-v3"): "device/vxnn/mosaic_entry.cpp",
    ("mosaic-int16-v5", "fused-pointwise-v4"): "device/vxnn/mosaic_entry.cpp",
    ("mosaic-int16-v6", "cooperative-v6"): "device/vxnn/mosaic_coop_entry.cpp",
    ("mosaic-int16-v7", "reuse-v7"): "device/vxnn/mosaic_reuse_entry.cpp",
    ("mosaic-int16-v8", "reuse-v8"): "device/vxnn/mosaic_reuse_entry.cpp",
}
HUNET_CALLBACKS = {"pack", "gemm", "scalar", "gn_apply", "add", "mul", "copy", "slice", "concat", "shift", "gn_stats", "gap"}
R13_LEGACY = {
    "hunet-fp16-v3": ("hunet-fp16-v2", "90334b363980dcb3d24c2aa03604a8e834e6dae77b4b31b0c0b8c1b43306cab9"),
    "mosaic-int16-v8": ("mosaic-int16-v7", "ff0446e45cb013772c772442cd22f3bb98ff216ee217c8c5606acb1fb9593a59"),
}


def sha(path):
    path = pathlib.Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError("REGULAR_NONLINK_PROGRAM_INPUT_REQUIRED " + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiled_sources(receipt, entry):
    """Derive project dependencies from the actual hashed compiler command.

    Receipt paths describe their original build host. They are interpreted as
    POSIX metadata, never opened against that old host or the current copy.
    """
    roots = set()
    for command in receipt["commands"]:
        for argument in command["argv"]:
            if argument.endswith("/" + entry):
                roots.add(posixpath.normpath(argument[:-len(entry)]))
    if len(roots) != 1 or not next(iter(roots)).startswith("/"):
        raise ValueError("ONE_ACTUAL_ENTRY_COMPILATION_ROOT_REQUIRED")
    build_root = next(iter(roots))
    result = {}
    def add(name, digest):
        canonical = posixpath.normpath(name)
        if canonical.startswith(("/", "../")) or canonical in (".", "..") or "\\" in canonical or not re.fullmatch("[0-9a-f]{64}", digest):
            raise ValueError("CANONICAL_ACTUAL_BUILD_SOURCE_REQUIRED")
        if canonical in result and result[canonical] != digest:
            raise ValueError("CONFLICTING_ACTUAL_BUILD_DEPENDENCY " + canonical)
        result[canonical] = digest
    for name, digest in receipt["sources"].items():
        add(name, digest)
    if entry not in result:
        raise ValueError("ACTUAL_ENTRY_SOURCE_RECEIPT_REQUIRED")
    for name, digest in receipt["dependencies"].items():
        canonical = posixpath.normpath(name)
        prefix = build_root + "/"
        if canonical.startswith(prefix) and not canonical.startswith(prefix + "build/"):
            add(canonical[len(prefix):], digest)
    return result


def selected(root, name, variant, schema_sha, pin_header, pins):
    root = pathlib.Path(root)
    entry = PROGRAM_ENTRIES.get((name, variant))
    if entry is None:
        raise ValueError("EXPLICIT_PUBLISHED_PROGRAM_RECIPE_REQUIRED " + name)
    directory = root / "data/nnrt/programs" / name
    family_path = directory / "family.json"
    if not family_path.is_file():
        raise ValueError("INDEPENDENT_ACTUAL_RV32_PUBLICATION_REQUIRED " + name)
    family = json.loads(family_path.read_bytes())
    if family.get("variant") != variant or family.get("schema_sha256") != schema_sha:
        raise ValueError("EXACT_PROGRAM_VARIANT_AND_SCHEMA_REQUIRED " + name)
    receipt_path = directory / "rv32-build.json"
    if sha(receipt_path) != family.get("program_receipt_sha256"):
        raise ValueError("ACTUAL_PROGRAM_RECEIPT_CHANGED " + name)
    receipt = json.loads(receipt_path.read_bytes())
    if (type(receipt.get("natural_exit")) is not int or receipt["natural_exit"] != 0
            or receipt.get("target_executed") is not False or receipt.get("gpu_raw_cycles", 0) is not None
            or not receipt.get("object_ownership") or not receipt.get("stack")
            or not 0 < receipt["stack"]["bytes"] < 8192 - 64):
        raise ValueError("ACTUAL_COMPLETE_STATIC_BUILD_REQUIRED " + name)
    if sha(directory / "kernel.elf") != receipt["elf"]["sha256"]:
        raise ValueError("PUBLISHED_ACTUAL_ELF_CHANGED " + name)
    for filename, key in [("code.bin", "code_sha256"), ("args-schema.bin", "schema_sha256")]:
        if sha(directory / filename) != family.get(key):
            raise ValueError("PUBLISHED_PROGRAM_BYTES_CHANGED " + name + "/" + filename)
    if (directory / "code.bin").stat().st_size != 65536 or receipt["flat"] != dict(bytes=65536, sha256=family["code_sha256"]):
        raise ValueError("EXACT_BUILT_64K_CODE_REQUIRED " + name)
    if family["code_sha256"] == family.get("legacy", {}).get("code_sha256"):
        raise ValueError("NEW_CODE_IDENTITY_REQUIRED " + name)
    if name in R13_LEGACY:
        old_name, old_code = R13_LEGACY[name]
        olddir = root / "data/nnrt/programs" / old_name
        old = json.loads((olddir / "family.json").read_bytes())
        if (family.get("legacy") != dict(directory="data/nnrt/programs/" + old_name,
                                         family_sha256=sha(olddir / "family.json"), code_sha256=old_code)
                or old.get("code_sha256") != old_code or sha(olddir / "code.bin") != old_code
                or family["code_sha256"] == old_code):
            raise ValueError("EXACT_FROZEN_PREDECESSOR_AND_NEW_CODE_REQUIRED " + name)
        if not receipt.get("commands") or any(type(c.get("natural_exit")) is not int or c["natural_exit"] != 0 for c in receipt["commands"]):
            raise ValueError("ALL_ACTUAL_BUILD_COMMANDS_MUST_COMPLETE " + name)
    if name.startswith("mosaic"):
        if sha(directory / "rodata.bin") != family.get("rodata_sha256") or receipt["readonly"] != dict(bytes=6307840, sha256=family["rodata_sha256"]):
            raise ValueError("FROZEN_MOSAIC_READONLY_CHANGED")
    elif receipt.get("dispatch_variant") != variant or set(receipt.get("audited_callback_names", [])) != HUNET_CALLBACKS:
        raise ValueError("ALL_12_HUNET_CALLBACKS_MUST_BE_AUDITED")
    if name == "mosaic-int16-v8":
        if family.get("build_variant") != "reuse-v7" or receipt.get("variant") != "reuse-v7":
            raise ValueError("EXACT_REUSE_V8_PUBLICATION_V7_BUILD_MAPPING_REQUIRED")
        expected = dict(private_abi=4, schedule_version=2, weight_layout_version=2,
                        work_layout_version=1, profile_wire_version=2)
        if any(type(meta.get(key)) is not int or meta[key] != value
               for meta in (family, receipt) for key, value in expected.items()):
            raise ValueError("EXPLICIT_REUSE_V8_BUILD_METADATA_REQUIRED")
    if name == "hunet-fp16-v3":
        entry_commands = [c for c in receipt["commands"] if any(a.endswith("/" + entry) for a in c["argv"])]
        if (len(receipt.get("audited_callback_names", [])) != 12 or not entry_commands
                or any([a for a in c["argv"] if a.startswith("-DVXNN_HUNET_TILE_N=")] != ["-DVXNN_HUNET_TILE_N=2"] for c in entry_commands)):
            raise ValueError("ACTUAL_HUNET_TILE2_ENTRY_COMPILATION_REQUIRED")
    rows = family.get("sources", [])
    sources = {}
    for row in rows:
        path, digest = row["path"], row["sha256"]
        relative = pathlib.PurePosixPath(path)
        if relative.is_absolute() or ".." in relative.parts or "\\" in path or path in sources or not re.fullmatch("[0-9a-f]{64}", digest):
            raise ValueError("CANONICAL_UNIQUE_PROGRAM_SOURCE_REQUIRED")
        if sha(root / path) != digest:
            raise ValueError("PUBLISHED_GPU_SOURCE_DRIFT " + path)
        sources[path] = digest
    if not sources:
        raise ValueError("ACTUAL_PROGRAM_SOURCE_BINDINGS_REQUIRED")
    required = compiled_sources(receipt, entry)
    if name in R13_LEGACY:
        mandatory = ({"device/vxnn/hunet_entry.cpp", "device/vxnn/hunet_grouped_gemm.h"}
                     if name.startswith("hunet") else
                     {"device/vxnn/mosaic_reuse_entry.cpp", "device/vxnn/mosaic_wire_reuse_v4.cpp",
                      "device/vxnn/mosaic_work_layout_v1.h", "device/mosaic_s8/reuse_schedule_v2.h"})
        if not mandatory <= required.keys():
            raise ValueError("R13_ACTUAL_COMPILED_COMPONENT_CLOSURE_REQUIRED " + name)
    # These two immutable schema/constant-pool preparation inputs were also
    # bound by HUNet v1; they are explicit metadata, not compiled C++ units.
    metadata = set() if name.startswith("mosaic") else {"scripts/unified/hunet_schema.py", "scripts/unified/hunet_rodata.py"}
    if set(sources) != set(required) | metadata or any(sources.get(path) != digest for path, digest in required.items()):
        raise ValueError("PUBLISHED_SOURCES_NOT_CLOSED_OVER_ACTUAL_BUILD " + name)
    header = (root / pin_header).read_text(encoding="utf8")
    for symbol, value in pins(family, sources).items():
        if symbol + '=\"' + value + '\"' not in header:
            raise ValueError("ACTUAL_PROGRAM_PIN_DIFFERS " + symbol)
    return directory
