"""Combine same-candidate native XSA metadata and native post-bit SYSDEF.

Vivado 2020.1 write_sysdef alone is an SDK container, not a Vitis XSA.
The layout below matches local 2020.1 bit-containing XSA exports: sysdef.xml,
HWH/init payload, bit, and xsa.xml/json with a FULL_BIT file entry.
No device/address/clock metadata is borrowed from the reference platform.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import hashlib
import json
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
art = root / 'build/artifacts'
with ZipFile(art / 'gpgpu_no_bit.xsa') as meta, ZipFile(art / 'gpgpu.sysdef') as sys:
    sys_files = {n: sys.read(n) for n in sys.namelist()}
    meta_files = {n: meta.read(n) for n in meta.namelist()}
    for name in sys_files.keys() & meta_files.keys():
        assert sys_files[name] == meta_files[name], 'Mixed hardware metadata: ' + name
    sys_xml = ET.fromstring(sys_files['sysdef.xml'])
    assert sys_xml.find("File[@Type='BIT']").get('Name') == 'top.bit'
    assert sys_files['top.bit'] == (art / 'top.bit').read_bytes()
    metadata = json.loads(meta_files['xsa.json'])
    assert not any(f.get('type') == 'FULL_BIT' for f in metadata.get('files', []))
    metadata.setdefault('files', []).append({'name': 'top.bit', 'type': 'FULL_BIT'})
    xml = ET.fromstring(meta_files['xsa.xml'])
    files = xml.find('DSA/Files')
    assert files is not None
    ET.SubElement(files, 'File', {'Type': 'FULL_BIT', 'Name': 'top.bit'})
    payload = {n: data for n, data in meta_files.items() if n != 'hwdef.xml'}
    payload.update(sys_files)
    payload['xsa.json'] = json.dumps(metadata, indent=2).encode('utf-8')
    payload['xsa.xml'] = ET.tostring(xml, encoding='utf-8', xml_declaration=True)
    with ZipFile(art / 'gpgpu_with_bit.xsa', 'w', ZIP_DEFLATED) as out:
        for name, data in payload.items():
            out.writestr(name, data)
    manifest = {name: {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                for name, data in payload.items()}
    (root / 'evidence/xsa_members.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('XSA_PACKAGED: same-candidate native SYSDEF payload and XSA metadata; Vitis import verification required')
