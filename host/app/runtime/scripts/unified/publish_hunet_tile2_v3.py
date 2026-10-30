"""Publish an independent span32-tile2-v3 program from the actual static build."""
import argparse
import hashlib
import json
from scripts.unified.program_identity import sha, HUNET_CALLBACKS
from scripts.unified.r13_program_publication import prepare, finish
from scripts.unified.hunet_schema import make_schema
from scripts.unified.hunet_tile2_program import CURRENT, VARIANT, PIN_HEADER, SCHEMA_SHA256, pins, selected_directory


def publish(root, build):
    root, build, olddir, old, rec, sources, family = prepare(root, build, CURRENT, "device/vxnn/hunet_entry.cpp")
    if (rec.get("dispatch_variant") != VARIANT or len(rec.get("audited_callback_names", [])) != 12
            or set(rec["audited_callback_names"]) != HUNET_CALLBACKS):
        raise ValueError("ACTUAL_ALL_CALLBACKS_HUNET_TILE2_BUILD_REQUIRED")
    old_sources = {row["path"]: row["sha256"] for row in old["sources"]}
    for path in ("scripts/unified/hunet_schema.py", "scripts/unified/hunet_rodata.py"):
        if sha(root / path) != old_sources[path]:
            raise ValueError("FROZEN_HUNET_SCHEMA_OR_RODATA_SOURCE_CHANGED " + path)
        sources[path] = old_sources[path]
    schema = make_schema()
    if hashlib.sha256(schema).hexdigest() != SCHEMA_SHA256 or sha(olddir / "args-schema.bin") != SCHEMA_SHA256:
        raise ValueError("FROZEN_HUNET_SCHEMA_CHANGED")
    family.update(variant=VARIANT, implementation="controlled-span32-tile2-k2-v3", schema_sha256=SCHEMA_SHA256)
    return finish(root, build, olddir, CURRENT, family, sources, schema, PIN_HEADER, pins(family, sources),
                  selected_directory, "scripts/unified/publish_hunet_tile2_v3.py")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True)
    p.add_argument("--build", required=True)
    a = p.parse_args()
    print(json.dumps(publish(a.root, a.build), indent=2))
