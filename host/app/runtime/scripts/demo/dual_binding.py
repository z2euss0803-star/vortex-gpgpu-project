"""Offline dual-release identity DAG. Never synthesize historical board PASS."""
import hashlib,json,pathlib,struct
from scripts.demo.mosaic_catalog import digest
GENERATED_BINDING_HEADERS=frozenset(('driver/vxl_mosaic_release.h','driver/vxl_hunet_source.h','driver/vxl_hybrid_release.h','driver/vxl_fused_release.h','generated/demo/mosaic_release_template.h','generated/demo/release_assets.h'))
SOURCE_ROOTS=frozenset(('adapters','core','demo','device','driver','include','mosaic','scripts','userspace','viewer','tools','vendor','generated','config','contracts','tests','petalinux'))
def inventory_source(root):
    root=pathlib.Path(root).resolve();files={}
    for prefix in sorted(SOURCE_ROOTS):
        folder=root/prefix
        if folder.is_symlink():raise ValueError('source root symlink')
        if not folder.exists():continue
        for f in folder.rglob('*'):
            name=f.relative_to(root).as_posix()
            if any(x in ('__pycache__','.git','.pytest_cache') for x in f.relative_to(folder).parts):continue
            if f.is_symlink():raise ValueError('source symlink '+name)
            if not f.is_file() or name in GENERATED_BINDING_HEADERS or f.suffix in ('.o','.pyc','.ko'):continue
            b=f.read_bytes();files[name]=dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    for name in ('Makefile','Makefile.demo','Makefile.mosaic','.gitattributes','Kbuild'):
        f=root/name
        if f.is_symlink():raise ValueError('source top symlink')
        if f.is_file():
            b=f.read_bytes();files[name]=dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    return files
def source_digest(files):
    if not files:raise ValueError('empty source identity')
    for name,r in files.items():
        if type(name)!=str or '\\' in name or ':' in name or name.startswith('/') or any(x in ('','.','..') for x in name.split('/')) or name in GENERATED_BINDING_HEADERS:raise ValueError('source identity path/cycle')
        if set(r)!={'bytes','sha256'} or type(r['bytes'])!=int or r['bytes']<0:raise ValueError('source byte count')
        digest(r['sha256'])
    return hashlib.sha256(json.dumps(dict(schema=1,scope='NORMALIZED_EXACT_SOURCE_WITH_GENERATED_BINDING_EXCLUSIONS',files=files),sort_keys=True,separators=(',',':')).encode()).hexdigest()
def _checked_hunet(raw):
    if type(raw)!=bytes or len(raw)!=512 or struct.unpack_from('<8I',raw)!=(0x44424c48,3,512,4,9,15,0,0) or any(raw[352:384]) or any(raw[480:512]):raise ValueError('program9 binding schema/reserved/generation')
    if any(not any(raw[i:i+32]) for i in list(range(32,352,32))+list(range(384,480,32))):raise ValueError('missing program9 identity')
    return raw
def rebind_hunet(previous,source_sha,cpu_sha):
    b=bytearray(_checked_hunet(previous));b[96:128]=digest(source_sha);b[256:288]=digest(cpu_sha)
    return _checked_hunet(bytes(b))
def pending_hunet_gate(binding):
    b=_checked_hunet(binding)
    # Flags0 intentionally refuses measured inference before any START. New CPU
    # binding is not entitled to the prior executable's numeric approval.
    return b'VXFUSG1\0'+bytes(8)+hashlib.sha256(b).digest()+bytes(80)
