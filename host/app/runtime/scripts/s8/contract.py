"""Generate static field-offset assertions from the private JSON contract."""
from pathlib import Path
import hashlib,json
from scripts.s8.catalog import load_catalog

def generate(root: Path,out: Path) -> dict:
    wire=json.loads((root/'contracts/s8/wire.json').read_text(encoding='utf-8'))
    catalog=load_catalog(root/'contracts/s8/cases.json')
    if (wire['profile'],wire['program'],wire['abi_major'],wire['outer_abi_bytes'],wire['job_bytes'],wire['result_bytes'],wire['binding_bytes'])!=(5,11,1,176,128,256,512):
        raise ValueError('wire identity changed')
    out.mkdir(parents=True,exist_ok=True)
    text=['/* generated; private wire offset assertions */','#include <vxl/s8_wire.h>']
    for kind in ('job','result','binding'):
        for name,offset in wire[kind+'_offsets'].items():
            text.append('S8_ASSERT(__builtin_offsetof(struct vxl_s8_'+kind+'_v1,'+name+')=='+str(offset)+');')
    data=('\n'.join(text)+'\n').encode()
    (out/'s8_contract_generated.h').write_bytes(data)
    receipt=dict(version=1,sha256=hashlib.sha256(data).hexdigest(),case_count=len(catalog['cases']),phase_events=wire['phase_events'],binding_digests=wire['binding_digests'],reserved_job_offset=108,reserved_result_offset=128)
    (out/'contract.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n',encoding='utf-8')
    return receipt

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();print(json.dumps(generate(Path('.'),args.out),sort_keys=True))
