"""Small auditable RV32/Vortex encoder; no toolchain or libc dependency.
Every active thread computes one element. MHARTID is core*16+warp*4+thread.
Encoding is checked by actual Vortex RTL execution, not just this encoder.
"""
from pathlib import Path
import struct
r=Path(__file__).resolve().parents[1];out=r/'kernel';out.mkdir(exist_ok=True)
code=[];listing=[];assembly=[]
def emit(word,text):
    listing.append(f'{0x10000000+4*len(code):08x}: {word:08x}  {text}')
    code.append(word)
    if text.startswith(('wspawn','tmc')): text=f'.word 0x{word:08x} # '+text
    assembly.append(text.replace('common_entry: ', 'common_entry:\n  '))
def imm(op,rd,funct,rs,im):return ((im&4095)<<20)|(rs<<15)|(funct<<12)|(rd<<7)|op
def reg(op,rd,funct,rs1,rs2):return (rs2<<20)|(rs1<<15)|(funct<<12)|(rd<<7)|op
emit(imm(0x13,5,0,0,4),'addi x5,x0,4')
emit(0x10000337,'lui x6,0x10000')
emit(imm(0x13,6,0,6,16),'addi x6,x6,16 # common_entry')
emit(reg(0x0b,0,1,5,6),'wspawn x5,x6 # activate warps 1..3; warp0 falls through')
emit(imm(0x13,5,0,0,15),'common_entry: addi x5,x0,15')
emit(reg(0x0b,0,0,5,0),'tmc x5 # all four lanes active')
emit(imm(0x73,7,2,0,0x340),'csrr x7,mscratch # DCR argument pointer')
emit(imm(0x73,8,2,0,0xf14),'csrr x8,mhartid # 0..15')
emit(imm(0x13,8,1,8,2),'slli x8,x8,2')
for rd,offset in [(9,0),(10,4),(11,8)]:emit(imm(3,rd,2,7,offset),f'lw x{rd},{offset}(x7)')
for rd in [9,10,11]:emit(reg(0x33,rd,0,rd,8),f'add x{rd},x{rd},x8')
emit(imm(3,12,2,9,0),'lw x12,0(x9)')
emit(imm(3,13,2,10,0),'lw x13,0(x10)')
emit(reg(0x33,14,0,12,13),'add x14,x12,x13')
emit(reg(0x23,0,2,11,14),'sw x14,0(x11)')
emit(0x0330000f,'fence rw,rw')
emit(reg(0x0b,0,0,0,0),'tmc x0 # terminate each warp')
emit(0x0000006f,'j . # unreachable guard')
assert len(code)==22 and code[3]==0x0062900b and code[18]==0x00e5a023
(out/'vecadd16.bin').write_bytes(struct.pack('<'+'I'*len(code),*code))
(out/'vecadd16.hex').write_text('\n'.join(f'{w:08x}' for w in code)+'\n')
(out/'vecadd16.lst').write_text('\n'.join(listing)+'\n')
(out/'vecadd16.S').write_text('# Fixed load address 0x10000000. Vortex v2.3 custom opcodes.\n# Native encoder used for the delivered binary; optional GNU assembly form.\n.option norvc\n.option norelax\n.section .text.start,"ax",@progbits\n.balign 4\n.globl _start\n_start:\n  '+ '\n  '.join(assembly)+'\n')
(r/'sw/vecadd16.h').write_text('#ifndef VECADD16_H\n#define VECADD16_H\n#include <stdint.h>\nstatic const uint32_t vecadd16_code[] = {\n'+',\n'.join(f'  0x{w:08x}u' for w in code)+'\n};\n#endif\n')
print(f'KERNEL_ENCODED: {len(code)} words, 4 warps x 4 threads, 16 elements')
