"""Publish v8 point-layout code while preserving the actual reuse-v7 recipe receipt."""
import argparse
import hashlib
import json
from scripts.unified.program_identity import sha
from scripts.unified.r13_program_publication import prepare, finish
from scripts.unified.mosaic_schema_reuse_v4 import make_schema_reuse_v4
from scripts.unified.mosaic_reuse_v8_program import CURRENT, VARIANT, BUILD_VARIANT, PIN_HEADER, pins, selected_directory


def publish(root, build):
    root, build, olddir, old, rec, sources, family = prepare(root, build, CURRENT, "device/vxnn/mosaic_reuse_entry.cpp")
    expected = dict(private_abi=4, schedule_version=2, weight_layout_version=2,
                    work_layout_version=1, profile_wire_version=2)
    if (rec.get("variant") != BUILD_VARIANT
            or any(type(rec.get(k)) is not int or rec[k] != value for k, value in expected.items())):
        raise ValueError("ACTUAL_ABI4_REUSE_V7_RECIPE_REQUIRED_FOR_V8")
    if (rec.get("readonly") != dict(bytes=6307840, sha256=sha(build / "rodata.bin"))
            or (build / "rodata.bin").stat().st_size != 6307840
            or sha(build / "rodata.bin") != old["rodata_sha256"]):
        raise ValueError("FROZEN_MOSAIC_READONLY_CHANGED")
    schema = make_schema_reuse_v4()
    schema_sha = hashlib.sha256(schema).hexdigest()
    schema_path = "scripts/unified/mosaic_schema_reuse_v4.py"
    if (schema_sha != old["schema_sha256"] or sha(olddir / "args-schema.bin") != schema_sha
            or old.get("schema_source") != dict(path=schema_path, sha256=sha(root / schema_path))):
        raise ValueError("FROZEN_MOSAIC_REUSE_SCHEMA_CHANGED")
    family.update(variant=VARIANT, build_variant=BUILD_VARIANT, schema_sha256=schema_sha,
                  rodata_sha256=rec["readonly"]["sha256"],
                  implementation="reuse-spatial2-weight2-point-layout-fix-v8",
                  schema_source=dict(path=schema_path, sha256=sha(root / schema_path)), **expected)
    return finish(root, build, olddir, CURRENT, family, sources, schema, PIN_HEADER, pins(family, sources),
                  selected_directory, "scripts/unified/publish_mosaic_reuse_v8.py")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True)
    p.add_argument("--build", required=True)
    a = p.parse_args()
    print(json.dumps(publish(a.root, a.build), indent=2))
