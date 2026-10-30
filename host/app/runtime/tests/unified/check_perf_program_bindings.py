"""Check actual R4 program publication and reject corrupted program bindings."""
import hashlib
import json
import pathlib
import shutil
import sys
import tempfile


def main():
    root = pathlib.Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(root))
    from scripts.unified.hunet_program import selected_directory as hunet
    from scripts.unified.mosaic_program import selected_directory as mosaic
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    families = [
        (hunet, "hunet-fp16-v2", "90334b363980dcb3d24c2aa03604a8e834e6dae77b4b31b0c0b8c1b43306cab9"),
        (mosaic, "mosaic-int16-v3", "8157dfd8a05d2ba6ec0e3ef9d6dd8bd545078afc8ce18e46856c9be2babe82d4"),
    ]
    rejected = 0
    for select, name, code_sha in families:
        directory = select(root)
        assert directory.name == name
        family = json.loads((directory / "family.json").read_bytes())
        receipt = json.loads((directory / "rv32-build.json").read_bytes())
        assert sha(directory / "code.bin") == family["code_sha256"] == code_sha
        assert sha(directory / "rv32-build.json") == family["program_receipt_sha256"]
        assert receipt["natural_exit"] == 0 and receipt["target_executed"] is False
        assert receipt["gpu_raw_cycles"] is None and receipt["stack"]["bytes"] < 8192 - 64
        assert sha(directory / "kernel.elf") == receipt["elf"]["sha256"]
        assert sha(directory / "args-schema.bin") == family["schema_sha256"]
        if name.startswith("hunet"):
            assert len(receipt["audited_callback_names"]) == 12
        else:
            assert sha(directory / "rodata.bin") == family["rodata_sha256"]
        # A private exact byte copy, not a mock family. Change only one input
        # at a time and require fresh publication admission to fail closed.
        with tempfile.TemporaryDirectory(dir=root / "build", prefix="r4-bind-negative-") as tmp:
            private = pathlib.Path(tmp)
            paths = [r["path"] for r in family["sources"]]
            paths += ["include/vxnn/" + ("hunet_span_program.hpp" if name.startswith("hunet") else "mosaic_ro64_program.hpp")]
            for relative in paths:
                target = private / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / relative, target)
            copy = private / "data/nnrt/programs" / name
            shutil.copytree(directory, copy)
            for relative in ["code.bin", "args-schema.bin", "rv32-build.json", "kernel.elf"] + (["rodata.bin"] if name.startswith("mosaic") else []):
                path = copy / relative
                original = path.read_bytes()
                path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
                try:
                    select(private)
                except (ValueError, json.JSONDecodeError):
                    rejected += 1
                else:
                    raise AssertionError("CORRUPTED_PUBLICATION_ACCEPTED " + name + "/" + relative)
                path.write_bytes(original)
            family_path = copy / "family.json"
            original_family = family_path.read_bytes()
            required = ["device/vxnn/hunet_entry.cpp", "device/hunet_fused_ops.h"] if name.startswith("hunet") else ["device/vxnn/mosaic_entry.cpp", "device/s8/isa.h"]
            for missing in required:
                changed = json.loads(original_family)
                assert any(r["path"] == missing for r in changed["sources"])
                changed["sources"] = [r for r in changed["sources"] if r["path"] != missing]
                source_path = private / missing
                source_bytes = source_path.read_bytes()
                source_path.unlink()
                family_path.write_text(json.dumps(changed), encoding="utf8")
                try:
                    select(private)
                except ValueError:
                    rejected += 1
                else:
                    raise AssertionError("ACTUAL_COMPILED_SOURCE_OMISSION_ACCEPTED " + name + "/" + missing)
                source_path.write_bytes(source_bytes)
                family_path.write_bytes(original_family)
            changed = json.loads(original_family)
            source_path = private / required[0]
            source_bytes = source_path.read_bytes()
            source_path.write_bytes(source_bytes + b"\n// forged source-to-receipt binding\n")
            for row in changed["sources"]:
                if row["path"] == required[0]:
                    row["sha256"] = sha(source_path)
            family_path.write_text(json.dumps(changed), encoding="utf8")
            try:
                select(private)
            except ValueError:
                rejected += 1
            else:
                raise AssertionError("SOURCE_NOT_BOUND_TO_ACTUAL_BUILD_ACCEPTED " + name)
            source_path.write_bytes(source_bytes)
            family_path.write_bytes(original_family)
            path = private / family["sources"][0]["path"]
            path.write_bytes(path.read_bytes() + b"\n// identity mutation\n")
            try:
                select(private)
            except ValueError:
                rejected += 1
            else:
                raise AssertionError("SOURCE_DRIFT_ACCEPTED " + name)
    print(json.dumps(dict(scope="R4_ACTUAL_PUBLICATION_BINDINGS_NOT_GPU_PASS", families=2, rejected_corruptions=rejected, gpu_raw_cycles=None)))


if __name__ == "__main__":
    main()
