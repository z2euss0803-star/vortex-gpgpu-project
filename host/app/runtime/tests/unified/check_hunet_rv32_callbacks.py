"""Check actual ELF callback coverage in the saved typed stack audit."""
import json,pathlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from scripts.unified.audit_int16_layout import elf_info
from scripts.s8.audit_rv32 import sections

def check(directory):
 directory=pathlib.Path(directory);raw,h,loads,symbols=elf_info(directory/'kernel.elf');_,funcs,_=sections(raw,h)
 needed={n for n in funcs if n.endswith('EPv') and 'hunet_controlled_' in n}
 audit=json.loads((directory/'stack-audit.json').read_text());indirect=[set(x['targets']) for x in audit['indirect'] if 'vx_spawn.c' in x.get('source',[])]
 assert needed and indirect,'actual controlled callbacks and spawn indirect sites required'
 for targets in indirect:assert targets==needed,'CONTROLLED_CALLBACKS_NOT_FULLY_AUDITED missing='+repr(sorted(needed-targets))+' extra='+repr(sorted(targets-needed))
 print(json.dumps(dict(scope='ACTUAL_ELF_TYPED_CALLBACK_COVERAGE_NOT_GPU',callbacks=len(needed),indirect_sites=len(indirect),stack_bytes=audit['bytes'])))

if __name__=='__main__':check(sys.argv[1])
