"""Reproduce H0 resource facts from the archived XSA; do not certify a board."""
import argparse
import copy
import hashlib
import json
import pathlib
import xml.etree.ElementTree as ET
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def extract_h0(root=ROOT):
    path = root / 'vendor/h0/gpgpu_with_bit.xsa'
    manifest = json.loads((root / 'vendor/h0/input_manifest_r4.json').read_text(encoding='utf-8-sig'))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != manifest['SHA256'].lower():
        raise ValueError('XSA does not match archived manifest')
    with zipfile.ZipFile(path) as z:
        xml = ET.fromstring(z.read('ps_bd.hwh'))
        bit = hashlib.sha256(z.read('top.bit')).hexdigest()
    if bit != manifest['EmbeddedAndExternalBitSHA256'].lower():
        raise ValueError('embedded bit mismatch')
    regs = []
    for name in ('HPM', 'cdma', 'status_gpio'):
        rows = [e for e in xml.iter('MEMRANGE') if e.get('INSTANCE') == name and e.get('MEMTYPE') == 'REGISTER']
        if len(rows) != 1:
            raise ValueError('ambiguous resource: ' + name)
        e = rows[0]
        base, high = int(e.get('BASEVALUE'), 0), int(e.get('HIGHVALUE'), 0)
        regs.append(dict(name=name, base=base, size=high-base+1))
    m = next(e for e in xml.iter('MODULE') if e.get('INSTANCE') == 'cdma')
    params = {e.get('NAME'): e.get('VALUE') for e in m.find('PARAMETERS')}
    port = next(e for e in m.iter('PORT') if e.get('NAME') == 'cdma_introut')
    connected = bool(port.get('SIGNAME'))  # SIGIS describes the port type, not wiring.
    return dict(profile='h0-r4', status='EXTRACTED', contract_major=1, contract_minor=0,
        xsa_sha256=digest, bit_sha256=bit, registers=regs,
        cdma=dict(address_bits=int(params['C_M_AXI_ADDR_WIDTH']), data_bits=int(params['C_M_AXI_DATA_WIDTH']),
                  dre=bool(int(params['C_INCLUDE_DRE'])), sg=bool(int(params['C_INCLUDE_SG'])), irq_connected=connected,
                  alignment=16, policy_chunk=4096),
        windows=[dict(domain='PS', bus_base=0, size=0x80000000, gpu_base=0, allocation='UNAPPROVED'),
                 dict(domain='PL', bus_base=0x500000000, size=0x80000000, gpu_base=0x80000000, allocation='DEVICE_MEMORY')],
        pl_cdma_only=dict(bus_base=0x580000000, size=0x80000000),
        fence=dict(gpu_base=0x10030000, size=64, ordinary_allocation=False),
        identity=dict(id=0x56580204, config=0x00040404, features=15, capability=0x00010001,
                      evidence='delivery/source/sw + reference RTL; live binding still required'),
        completion=dict(requires_jobs_increment=True, requires_done=True, requires_pending_zero=True,
                        requires_errors_zero=True, requires_fence=True, fault_reset='UNPROVEN'),
        commands=dict(start=1, ack_done=2),
        acceptance=dict(hardware_owner=None, evidence=None, ps_pool_approved=False))

def validate(p, board=False):
    baseline = extract_h0()
    if p['contract_major'] != 1:
        raise ValueError('incompatible major')
    for key in ('xsa_sha256', 'bit_sha256', 'registers', 'windows', 'cdma', 'completion', 'commands', 'fence', 'identity'):
        if p.get(key) != baseline[key]:
            raise ValueError('unsubstantiated H0 change: ' + key)
    if board and (p['status'] != 'FROZEN' or not p['acceptance'].get('evidence') or not p['acceptance'].get('ps_pool_approved')):
        raise ValueError('real hardware gates unresolved')

def generate():
    h0 = extract_h0()
    validate(h0)
    h1 = copy.deepcopy(h0)
    h1.update(profile='h1-proposed', status='PROPOSED', xsa_sha256=None, bit_sha256=None)
    h1['note'] = 'Candidate compatibility proposal only; hardware manifest and owner acceptance absent.'
    folder = ROOT / 'contracts/profiles'
    folder.mkdir(parents=True, exist_ok=True)
    for name, p in [('h0-r4', h0), ('h1-proposed', h1)]:
        (folder / (name+'.json')).write_text(json.dumps(p, indent=2)+'\n', encoding='utf-8')

if __name__ == '__main__':
    generate()
