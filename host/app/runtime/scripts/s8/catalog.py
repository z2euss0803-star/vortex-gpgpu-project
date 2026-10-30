"""Strict version-one case catalog, independent of ELF and hardware presence."""
from pathlib import Path
import json

VECTOR_SHA='c1040e4976018155577a4afab6fb53f31ab5e220473d8f286d49cb32912b9c22'
VARIANTS={201:2,202:1,203:7,204:2,205:1,206:1,207:1,208:2,209:1}
KEYS={'id','kind','k','a_bytes','b_bytes','result_bytes','phase_allowed','rule'}

def expected_cases():
    rows=[(1,'identity',0,0,0,0,False,'crt64'),
          (2,'compat',0,256,256,256,False,'vecadd63'),
          (3,'compat',8,128,64,128,False,'wmma8x4'),
          (4,'compat',0,16,40,20,False,'tex_bilinear'),
          (5,'compat',0,16,32,16,False,'tex_nearest')]
    for ident,k in zip(range(101,106),(1,2,3,2592,4096)):
        rows.append((ident,'positive',k,16*k,16*k,384,True,'frozen_'+str(ident-101)))
    for ident,k,rule in zip(range(110,116),(1,3,3,4096,4096,3),('zero','ones','minus_ones','min16','alternating','indices')):
        rows.append((ident,'positive',k,16*k,16*k,384,True,rule))
    rows.append((120,'positive',3,48,48,384,True,'core_warp_tags'))
    for family,count in VARIANTS.items():
        for variant in range(count):
            rows.append(((family<<8)|variant,'negative',2,32,32,384,False,str(family)+'_'+str(variant)))
    order=('id','kind','k','a_bytes','b_bytes','result_bytes','phase_allowed','rule')
    return [dict(zip(order,r)) for r in rows]

def load_catalog(path: Path) -> dict:
    doc=json.loads(Path(path).read_text(encoding='utf-8'))
    if set(doc)!={'version','seed','vector_sha256','cases'} or doc.get('version')!=1 or doc.get('seed')!=20261013 or doc.get('vector_sha256')!=VECTOR_SHA:
        raise ValueError('catalog identity/schema mismatch')
    rows=doc['cases']
    if type(rows) is not list or not rows or any(type(r) is not dict or set(r)!=KEYS for r in rows):
        raise ValueError('invalid case schema')
    for row in rows:
        if any(type(row[k]) is not int for k in ('id','k','a_bytes','b_bytes','result_bytes')) or type(row['phase_allowed']) is not bool:
            raise ValueError('case field type mismatch')
    if sorted(rows,key=lambda r:r['id'])!=sorted(expected_cases(),key=lambda r:r['id']):
        raise ValueError('case IDs/rules/payloads differ from frozen catalog')
    return doc
