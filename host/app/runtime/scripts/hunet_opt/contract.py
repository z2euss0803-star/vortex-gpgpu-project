"""Envelope validation only; catalog authentication is a separate mandatory gate."""
import json
from pathlib import Path
import struct

JOB_MAGIC = 0x32474c48
DESC_MAGIC = 0x32444748
DESC_BASE = 0x84000000
SCRATCH = 0x86c4df40
OWNER = 0x84040040
OWNER_END = 0x84140040
CONSTANTS = dict(schema=2, profile_id=4, program_id=10, binding_version=4,
                 binding_bytes=512, features=31, request_bytes=176,
                 cpu_evidence_bytes=384, job_bytes=128, job_magic=JOB_MAGIC,
                 descriptor_magic=DESC_MAGIC, policy_id=2,
                 descriptor_base=DESC_BASE, descriptor_slots=64,
                 descriptor_slot_bytes=4096, descriptor_header_bytes=128,
                 descriptor_node_bytes=96, max_nodes=32, scratch_base=SCRATCH,
                 scratch_bytes=4096, owner_base=OWNER, owner_elements=262144,
                 timing_base=0x84141000, timing_bytes=4096, logical_jobs=1023,
                 stages=100, cpu_jobs=13)


def require(ok, reason):
    if not ok:
        raise ValueError('OPT_' + reason)


def validate_contract(contract: dict) -> None:
    require(all(contract.get(k) == v for k, v in CONSTANTS.items()), 'CONTRACT_IDENTITY')
    require(len(contract.get('job_fields', [])) == 16 and
            len(contract.get('node_fields', [])) == 24, 'CONTRACT_FIELDS')
    # Half-open intervals within the already reserved weights object.
    ranges = [(0x80800000, 0x80800000 + 23685056),
              (DESC_BASE, DESC_BASE + 64*4096),
              (0x84040000, OWNER_END+64), (0x84141000, 0x84142000)]
    for i, (begin, end) in enumerate(ranges):
        require(0x80800000 <= begin < end <= 0x84800000, 'LAYOUT_RANGE')
        require(all(end <= a or z <= begin for a, z in ranges[:i]), 'LAYOUT_OVERLAP')


def load_contract(path: Path) -> dict:
    contract = json.loads(Path(path).read_text(encoding='utf-8'))
    validate_contract(contract)
    return contract


def descriptor_bytes(nodes):
    require(isinstance(nodes, int) and 1 <= nodes <= 32, 'NODE_COUNT')
    return (128 + 96*nodes + 63) & ~63


def _address(address):
    return address == 0 or 0x80800000 <= address < 0x84800000 or 0x85000000 <= address < 0x87067000


def validate_graph(job: bytes, descriptor: bytes) -> dict:
    require(len(job) == 128, 'JOB_SIZE')
    w = struct.unpack('<32I', job)
    require(w[:3] == (JOB_MAGIC, 2, 128) and w[3] in (16, 17), 'JOB_HEADER')
    require(1 <= w[4] <= w[5] <= 1023 and 0 <= w[6] <= w[7] < 100, 'JOB_RANGE')
    require(1 <= w[8] <= 64 and w[9] == DESC_BASE+(w[8]-1)*4096, 'JOB_SLOT')
    require(128 <= w[10] <= 4096 and w[10] % 64 == 0 and len(descriptor) == w[10], 'DESC_SIZE')
    require((w[11], w[12], w[15]) == (SCRATCH, 4096, 2) and not any(w[16:]), 'JOB_RESERVED')
    require((w[14] == 0 and w[13] == 0) or
            (1 <= w[14] <= 262144 and w[13] % 4 == 0 and
             OWNER <= w[13] and w[13] + w[14]*4 <= OWNER_END), 'OWNER_RANGE')
    h = struct.unpack('<32I', descriptor[:128])
    require(h[:4] == (DESC_MAGIC, 2, w[10], w[8]), 'DESC_HEADER')
    require(h[4:8] == w[4:8] and h[9:12] == (2, SCRATCH, 4096) and
            not any(h[12:]), 'DESC_RANGE_RESERVED')
    require(descriptor_bytes(h[8]) == w[10], 'DESC_NODE_SIZE')
    require(not any(descriptor[128+96*h[8]:]), 'DESC_PADDING')
    previous, stage = w[4]-1, w[6]
    nodes = []
    for i in range(h[8]):
        n = struct.unpack('<24I', descriptor[128+96*i:224+96*i])
        require(1 <= n[0] <= 15 and n[0] != 6, 'NODE_TYPE')
        require(n[2] == previous+1 and n[2] <= n[3] <= w[5], 'NODE_LOGICAL')
        require(stage <= n[1] <= w[7], 'NODE_STAGE')
        require(all(_address(x) for x in n[4:12]), 'NODE_ADDRESS')
        require(all(x<=33976320//4 for x in n[12:18]),'NODE_DIMENSION_RANGE')
        require(n[12]*n[14]*n[15] <= 33976320//4 and
                n[13]*n[16]*n[17] <= 33976320//4, 'NODE_SHAPE_OVERFLOW')
        previous, stage = n[3], n[1]
        nodes.append(n)
    require(previous == w[5] and nodes[0][1] == w[6] and stage == w[7], 'NODE_COVERAGE')
    require(w[3] != 16 or [n[0] for n in nodes] == [4, 5], 'GN_PAIR')
    return dict(block_id=w[8], first=w[4], last=w[5], first_stage=w[6],
                last_stage=w[7], nodes=nodes, scope='ENVELOPE_NOT_CATALOG_AUTHORIZATION')
