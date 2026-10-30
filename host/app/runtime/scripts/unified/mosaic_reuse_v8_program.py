"""Explicit R13 point-layout repair, compiled with the unchanged v7 recipe."""
import hashlib
import json
import pathlib
from scripts.unified.program_identity import selected, sha
from scripts.unified.mosaic_schema_reuse_v4 import make_schema_reuse_v4

CURRENT = "mosaic-int16-v8"
VARIANT = "reuse-v8"
BUILD_VARIANT = "reuse-v7"
PIN_HEADER = "include/vxnn/mosaic_reuse_v8_program.hpp"


def pins(family, sources):
    return dict(mosaic_reuse_v8_code_sha256=family["code_sha256"],
                mosaic_reuse_v8_validator_sha256=sources["device/vxnn/mosaic_wire_reuse_v4.cpp"],
                mosaic_reuse_v8_entry_sha256=sources["device/vxnn/mosaic_reuse_entry.cpp"],
                mosaic_reuse_v8_schedule_sha256=sources["device/mosaic_s8/reuse_schedule_v2.h"],
                mosaic_reuse_v8_work_layout_sha256=sources["device/vxnn/mosaic_work_layout_v1.h"],
                mosaic_reuse_v8_schema_sha256=family["schema_sha256"])


def selected_directory(root):
    root = pathlib.Path(root)
    family = json.loads((root / "data/nnrt/programs" / CURRENT / "family.json").read_bytes())
    schema_source = "scripts/unified/mosaic_schema_reuse_v4.py"
    if family.get("schema_source") != dict(path=schema_source, sha256=sha(root / schema_source)):
        raise ValueError("EXACT_REUSE_V8_SCHEMA_SOURCE_REQUIRED")
    return selected(root, CURRENT, VARIANT, hashlib.sha256(make_schema_reuse_v4()).hexdigest(), PIN_HEADER, pins)


def selected_name(root):
    return selected_directory(root).name
