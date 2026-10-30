"""Compile a bounded physical schedule from the unchanged logical catalog."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_BASE = '8015b72e9c55978c89df80fd4dabb1a617d7b167'


def hybrid_freezer(root):
    path = pathlib.Path(root) / 'scripts/hunet_hybrid/freeze.py'
    spec = importlib.util.spec_from_file_location('fused_baseline_freezer', str(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def freeze_catalog(root):
    root = pathlib.Path(root)
    baseline = hybrid_freezer(root).freeze_contract(root)
    text = (root / 'generated/hunet/catalog.h').read_text(encoding='utf-8')
    jobs = [[int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)u', line)]
            for line in text.splitlines() if line.startswith('{0x')]
    match = re.search(r'vxl_hunet_aux_bytes\[\d+\]=\{([^}]+)\}', text)
    if not match:
        raise ValueError('AUX_CATALOG_MISSING')
    aux = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', match.group(1)))
    matrices = [list(row) for row in struct.iter_unpack('<8I', aux)]
    contract = dict(schema=1, program_id=9, policy=1, source_base=SOURCE_BASE,
                    baseline=baseline, jobs=jobs, matrices=matrices)
    compile_schedule(contract)
    return contract


def compile_schedule(contract):
    if [contract.get(x) for x in ('schema', 'program_id', 'policy', 'source_base')] != [1, 9, 1, SOURCE_BASE]:
        raise ValueError('FUSION_CONTRACT_VERSION')
    hybrid_freezer(ROOT).verify_baseline(contract['baseline'])
    jobs, matrices = contract['jobs'], contract['matrices']
    if len(jobs) != 1023 or len(matrices) != 314:
        raise ValueError('FROZEN_CATALOG_COUNT')
    if any(len(j) != 32 or j[4] != i + 1 or j[30] >= 100 for i, j in enumerate(jobs)):
        raise ValueError('LOGICAL_SEQUENCE_OR_STAGE')
    try:
        raw = b''.join(struct.pack('<32I', *j) for j in jobs)
        aux = b''.join(struct.pack('<8I', *m) for m in matrices)
    except struct.error as error:
        raise ValueError('CATALOG_WORD_RANGE') from error
    ids = contract['baseline']['identities']
    if hashlib.sha256(raw).hexdigest() != ids['jobs_file'] or hashlib.sha256(aux).hexdigest() != ids['aux_file']:
        raise ValueError('FROZEN_LOGICAL_OR_MATRIX_IDENTITY')
    gpu, cpu, ranges = [], [], []
    triplets = index = 0
    while index < len(jobs):
        job = jobs[index]
        first = index + 1
        stage = job[30]
        if job[3] == 6:
            cpu.append(dict(stage=stage, sequence=first, elements=job[7]))
            ranges.append(dict(first=first, last=first, kind='CPU'))
            index += 1
            continue
        if job[3] != 1:
            if job[3] in (2, 3):
                raise ValueError('ORPHAN_MATRIX_OPERATION')
            gpu.append(dict(first=first, last=first, stage=stage, op=job[3],
                            logical_job=list(job)))
            ranges.append(dict(first=first, last=first, kind='GPU'))
            index += 1
            continue
        descriptors = []
        while index < len(jobs) and jobs[index][3] == 1 and jobs[index][30] == stage:
            if index + 2 >= len(jobs):
                raise ValueError('TRUNCATED_MATRIX_TRIPLET')
            pack, gemm, unpack = jobs[index:index + 3]
            if [x[3] for x in (pack, gemm, unpack)] != [1, 2, 3]:
                raise ValueError('MATRIX_TRIPLET_ORDER')
            if len({x[30] for x in (pack, gemm, unpack)}) != 1 or len({x[15] for x in (pack, gemm, unpack)}) != 1:
                raise ValueError('MATRIX_TRIPLET_LINK')
            if pack[13] != gemm[8] or gemm[13] != unpack[8]:
                raise ValueError('MATRIX_DATA_LINK')
            if any(pack[5:7] != x[5:7] or pack[16:30] != x[16:30] for x in (gemm, unpack)):
                raise ValueError('MATRIX_SHAPE_MISMATCH')
            matrix_index = (pack[15] - 0x87064100) // 32
            if pack[15] < 0x87064100 or (pack[15] - 0x87064100) % 32 or not 0 <= matrix_index < len(matrices):
                raise ValueError('MATRIX_AUX_ADDRESS')
            m, n, k, mp, np, kp, offset, count = matrices[matrix_index]
            if not (m > 0 and n > 0 and k > 0 and mp >= m and mp % 8 == 0 and
                    np >= count > 0 and np % 4 == 0 and kp >= k and kp % 8 == 0 and
                    offset + count <= n and gemm[7] == (mp // 8) * (np // 4)):
                raise ValueError('MATRIX_DESCRIPTOR_BOUNDS')
            descriptors.append(dict(matrix_index=matrix_index, matrix=list(matrices[matrix_index]),
                                    first=index + 1, last=index + 3, pack=list(pack),
                                    gemm=list(gemm), unpack=list(unpack)))
            triplets += 1
            index += 3
        if not 1 <= len(descriptors) <= 32:
            raise ValueError('STAGE_DESCRIPTOR_CAPACITY')
        # All chunks share stage-level tensor shapes and source/destination tensors.
        keys = {(d['pack'][8], d['gemm'][9], d['unpack'][10], d['unpack'][13]) for d in descriptors}
        if len(keys) != 1:
            raise ValueError('STAGE_TENSOR_IDENTITY')
        gpu.append(dict(first=first, last=index, stage=stage, op=15, descriptors=descriptors))
        ranges.append(dict(first=first, last=index, kind='GPU_FUSED'))
    matrix_stages = [item['stage'] for item in gpu if item['op'] == 15]
    if (triplets, len(matrix_stages), len(set(matrix_stages)), len(gpu), len(cpu)) != (314, 35, 35, 103, 13):
        raise ValueError('FUSION_SCHEDULE_COUNTS')
    if cpu != contract['baseline']['cpu_stages']:
        raise ValueError('CPU_ROLE_CHANGED')
    return dict(schema=1, scope='STATIC_FUSION_SCHEDULE_NOT_GPU_PASS', program_id=9,
                logical_jobs=1023, triplets=314, matrix_stages=35, gpu=gpu, cpu=cpu,
                logical_ranges=ranges, logical_sha256=ids['jobs_file'], aux_sha256=ids['aux_file'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    with args.out.open('w', encoding='utf-8', newline='\n') as output:
        json.dump(freeze_catalog(ROOT), output, indent=2)
        output.write('\n')
    print('FUSED_STATIC_CATALOG_FROZEN_NOT_GPU_PASS')
