"""Read legacy/extended RTJM without converting per-core CSR into wall time."""
import argparse
import hashlib
import io
import json
import pathlib
import struct

STAGES = ("input_pack", "weight_prepare", "begin_step", "read_decode", "output_restore", "point_stats")

def argument_tag(args):
    value = 2166136261
    for i, byte in enumerate(args):
        if i < 16 or i >= 48:
            value = ((value ^ byte) * 16777619) & 0xffffffff
    return value or 1

def analyze_stream(stream):
    jobs = profiled = total_bytes = 0
    scopes = set()
    counter = [dict() for _ in range(4)]
    prepare_ns = read_ns = 0
    first_index = last_index = None
    digest = hashlib.sha256()
    while True:
        prefix = stream.read(512)
        if not prefix:
            break
        if len(prefix) != 512:
            raise ValueError("INCOMPLETE_RTJM_HEADER_AT_" + str(total_bytes))
        magic, version, size, scope = struct.unpack_from("<4I", prefix)
        if magic != 0x4d4a5452 or (version, size) not in ((1, 512), (2, 1536)) or scope not in (0, 4):
            raise ValueError("UNKNOWN_RTJM_FORMAT_AT_" + str(total_bytes))
        tail = stream.read(size - 512)
        if len(tail) != size - 512:
            raise ValueError("INCOMPLETE_RTJM_BODY_AT_" + str(total_bytes))
        digest.update(prefix); digest.update(tail)
        index = struct.unpack_from("<Q", prefix, 16)[0]
        node, first, count, error = struct.unpack_from("<4I", prefix, 24)
        begin, end = struct.unpack_from("<2Q", prefix, 40)
        flags = struct.unpack_from("<I", prefix, 96)[0]
        if not node or not 0 < count <= 4096 or not begin or end < begin or flags & ~15:
            raise ValueError("INVALID_RTJM_JOB_METADATA")
        if last_index is not None and index != last_index + 1:
            raise ValueError("NONCONTIGUOUS_JOB_INDEX")
        if first_index is None:
            first_index = index
        last_index = index
        scopes.add(scope); jobs += 1; total_bytes += size
        if version == 1:
            if flags & 8:
                raise ValueError("LEGACY_RECORD_CANNOT_HAVE_STAGES")
            continue
        if error or not flags & 4 or not flags & 8:
            raise ValueError("STAGES_REQUIRE_SUCCESSFUL_COMPLETION_SNAPSHOT")
        args = prefix[128:256]
        words = struct.unpack("<32I", args)
        if words[0] not in (3, 4) or words[1] != 128 or words[28:30] != (first, count) or words[31] != 1:
            raise ValueError("STAGE_ARGUMENT_ABI_OPTIONS_OR_RANGE")
        schedule = 1 if words[0] == 3 else 2
        metadata_tag = argument_tag(args)
        prepare, read = struct.unpack_from("<2Q", prefix, 112)
        prepare_ns += prepare; read_ns += read; profiled += 1
        for core in range(4):
            record = tail[core * 256:(core + 1) * 256]
            h = struct.unpack_from("<16I", record)
            expected = (0x3350434d, 2, 256, 1, words[2], core, first, count, 1, 1, schedule, schedule, 2)
            if h[:13] != expected or h[14] or h[15] != metadata_tag or h[13] & ~63:
                raise ValueError("STAGE_HEADER_CORE_GEOMETRY_LAYOUT_OR_TIMER")
            for stage, name in enumerate(STAGES):
                cycles, units, logical_bytes, calls = struct.unpack_from("<4Q", record, 64 + stage * 32)
                if bool(h[13] & (1 << stage)) != bool(calls) or (not calls and (cycles or units or logical_bytes)):
                    raise ValueError("STAGE_PRESENCE_OR_UNMEASURED_NONZERO")
                if calls and not cycles:
                    raise ValueError("CSR_TIMER_UNAVAILABLE_NOT_ZERO_ELAPSED")
                if not calls:
                    continue
                aggregate = counter[core].setdefault(name, dict(cycles=0, units=0, logical_bytes=0, calls=0, jobs=0))
                for key, value in zip(("cycles", "units", "logical_bytes", "calls"), (cycles, units, logical_bytes, calls)):
                    aggregate[key] += value
                aggregate["jobs"] += 1
    if not jobs:
        raise ValueError("EMPTY_TRACE_HAS_NO_MEASUREMENT")
    return dict(format="mosaic-stage-analysis-v1", status="PARSED_RAW_EVIDENCE_NOT_NUMERIC_OR_PERFORMANCE_PASS",
                trace_sha256=digest.hexdigest(), bytes=total_bytes, jobs=jobs, profiled_jobs=profiled,
                first_index=first_index, last_index=last_index,
                scopes=["REAL_BOARD" if x == 4 else "HOST_PROTOCOL_TEST" for x in sorted(scopes)],
                timer_domain="DECLARED_PER_CORE_RV32_CSR64_DISTINCT_FROM_CONTROLLER32",
                cross_core_sum_as_elapsed=False, clock_hz=None, derived_elapsed_seconds=None,
                unmeasured_stages="ABSENT_NOT_ZERO_TIME", profile_prepare_ns=prepare_ns if profiled else None,
                profile_read_ns=read_ns if profiled else None,
                per_core=[dict(core=i, stages=stages) for i, stages in enumerate(counter)])

def analyze_bytes(data):
    return analyze_stream(io.BytesIO(data))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path)
    args = parser.parse_args()
    with args.trace.open("rb") as stream:
        result = analyze_stream(stream)
    result["source"] = str(args.trace.resolve())
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        with args.out.open("x", encoding="utf8", newline="\n") as output:
            output.write(text)
    else:
        print(text, end="")

if __name__ == "__main__":
    main()
