"""Real RV32 ELF/stack/instruction audit. Never simulation or board evidence."""
from pathlib import Path
import hashlib
import re
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
from audit_program import elf_info,package_program
def audit_rv32(elf:Path,receipt:dict)->dict:
    elf=Path(elf);attempt=elf.parent
    raw,_,loads,symbols=elf_info(elf)
    report=package_program(elf,attempt,10)
    if report['wire_bytes']>8388608:raise ValueError('CODE_CAPACITY')
    if '-ffp-contract=off' not in receipt['compile_flags'] or any('fast-math' in f for f in receipt['compile_flags']):raise ValueError('FP_CONTRACT')
    for rel,sha in receipt['sources'].items():
        if hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()!=sha:raise ValueError('SOURCE_DRIFT:'+rel)
    frames=[]
    for file in sorted(attempt.glob('*.su')):
        for line in file.read_text().splitlines():
            name,size,kind=line.rsplit('\t',2);frames.append(dict(function=name,bytes=int(size),kind=kind))
    if not frames or any(f['kind']!='static' or f['bytes']>=8192 for f in frames):raise ValueError('STACK_FRAME_CAPACITY_OR_UNKNOWN')
    disasm=(attempt/'kernel.disasm').read_text()
    stack_refs=len(re.findall(r'\b(?:lw|sw|flw|fsw)\b[^\n]*\(sp\)',disasm))
    report.update(scope='RV32_REAL_COMPILE_STATIC_AUDIT_NOT_GPU',stack_frames=frames,max_frame=max(f['bytes'] for f in frames),stack_memory_instruction_count=stack_refs,spill_interpretation='static stack references; not all are allocator spills',gpu_cycles=None,hardware_revalidated=False)
    return report
