"""Frozen RV32IM diagnostic payloads. Generation is not GPU evidence."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
CODE=0x10000000
def imm(op,rd,f,rs,x): return ((x&4095)<<20)|(rs<<15)|(f<<12)|(rd<<7)|op
def reg(op,rd,f,a,b,f7=0): return (f7<<25)|(b<<20)|(a<<15)|(f<<12)|(rd<<7)|op
def store(src,base,off=0): return (((off&4095)>>5)<<25)|(src<<20)|(base<<15)|(2<<12)|((off&31)<<7)|0x23
def add(rd,a,b): return reg(0x33,rd,0,a,b)
def mul(rd,a,b): return reg(0x33,rd,0,a,b,1)
def li(rd,n): return imm(0x13,rd,0,0,n)
def shift(rd,a,n): return imm(0x13,rd,1,a,n)
def load(rd,base,off=0): return imm(3,rd,2,base,off)
PRELUDE=[li(5,4),0x10000337,imm(0x13,6,0,6,16),reg(0xb,0,1,5,6),
         li(5,15),reg(0xb,0,0,5,0),imm(0x73,7,2,0,0x340),imm(0x73,8,2,0,0xf14)]
FINISH=[0x0330000f,reg(0xb,0,0,0,0),0x0000006f]

class Assembler:
    def __init__(self): self.code=PRELUDE.copy(); self.labels={}; self.fixups=[]
    def emit(self,*words): self.code.extend(words)
    def label(self,name): self.labels[name]=len(self.code)*4
    def skip_zero(self,rs,label): self.fixups.append((len(self.code),rs,label)); self.emit(0)
    def finish(self):
        for i,rs,label in self.fixups:
            off=self.labels[label]-i*4
            if not -4096<=off<4096 or off%4: raise ValueError('Branch range')
            v=off&8191
            self.code[i]=((v>>12)<<31)|(((v>>5)&63)<<25)|(rs<<15)|(((v>>1)&15)<<8)|(((v>>11)&1)<<7)|0x63
        return self.code+FINISH

def conv(a):
    a.emit(li(17,36),reg(0x33,14,5,13,17,1),reg(0x33,17,7,13,17,1),li(16,6),
           reg(0x33,15,5,17,16,1),reg(0x33,16,7,17,16,1),
           shift(19,14,2),add(19,11,19),load(18,19),
           li(22,36),mul(22,14,22),add(22,10,22),
           shift(19,15,3),add(19,19,16),shift(19,19,2),add(19,9,19))
    for ky in range(3):
        for kx in range(3):
            a.emit(load(20,19,ky*32+kx*4),load(21,22,ky*12+kx*4),mul(20,20,21),add(18,18,20))
    a.emit(shift(19,13,2),add(19,12,19),store(18,19))

def relu(a):
    a.emit(shift(19,13,2),add(20,9,19),load(20,20),imm(0x13,21,5,20,0x41f),
           imm(0x13,21,4,21,-1),reg(0x33,20,7,20,21),add(19,12,19),store(20,19))

def pool(a):
    a.emit(li(17,9),reg(0x33,14,5,13,17,1),reg(0x33,17,7,13,17,1),li(16,3),
           reg(0x33,15,5,17,16,1),reg(0x33,16,7,17,16,1),
           li(19,36),mul(19,14,19),li(17,12),mul(17,15,17),add(19,19,17),
           shift(16,16,1),add(19,19,16),shift(19,19,2),add(19,9,19),load(20,19))
    for off in (4,24,28):
        a.emit(load(21,19,off),reg(0x33,22,2,20,21),reg(0x33,22,0,0,22,0x20),
               reg(0x33,17,4,20,21),reg(0x33,17,7,17,22),reg(0x33,20,4,20,17))
    a.emit(shift(19,13,2),add(19,12,19),store(20,19))

def fc(a):
    a.emit(shift(19,13,2),add(19,11,19),load(18,19),li(22,72),mul(22,13,22),add(22,10,22))
    for j in range(18): a.emit(load(20,9,j*4),load(21,22,j*4),mul(20,20,21),add(18,18,20))
    a.emit(shift(19,13,2),add(19,12,19),store(18,19))

def program_words(pid):
    if pid not in (2,3,4,5): raise ValueError('Unknown frozen program')
    a=Assembler()
    a.emit(*(load(r,7,o) for r,o in ((9,16),(10,20),(11,24),(12,28))))
    size={2:72,3:72,4:18,5:2}[pid]
    for round_id in range(2 if size==72 else 1):
        a.emit(imm(0x13,13,0,8,round_id*64),imm(0x13,23,3,13,size),reg(0xb,24,2,23,0))
        a.skip_zero(23,'join'+str(round_id))
        {2:conv,3:relu,4:pool,5:fc}[pid](a)
        a.label('join'+str(round_id)); a.emit(reg(0xb,0,3,24,0))
    return a.finish()

def generate_program(program_id):
    words=program_words(program_id)
    if len(words)*4>4096: raise ValueError('Code exceeds contract')
    return struct.pack('<'+'I'*len(words),*words)+bytes(4096-len(words)*4)

def generate(root):
    root=Path(root); (root/'kernel/cnn').mkdir(parents=True,exist_ok=True)
    (root/'generated/cnn').mkdir(parents=True,exist_ok=True)
    lines=['/* Generated from audited RV32 words; do not edit. */','#ifndef VXL_CNN_PROGRAMS_H','#define VXL_CNN_PROGRAMS_H']
    manifest={'scope':'PROGRAM_GENERATION_NOT_GPU','programs':[]}
    for pid in (2,3,4,5):
        blob=generate_program(pid); words=program_words(pid)
        (root/f'kernel/cnn/id{pid}.bin').write_bytes(blob)
        lines.append(f'#define VXL_CNN_SHA_{pid} "{hashlib.sha256(blob).hexdigest()}"')
        lines.append(f'static const vxl_u32 vxl_cnn_program_{pid}[1024]={{')
        lines += [' '+','.join(f'0x{w:08x}U' for w in words[i:i+8])+',' for i in range(0,len(words),8)]
        lines.append('};')
        manifest['programs'].append(dict(id=pid,bytes=4096,instruction_bytes=len(words)*4,sha256=hashlib.sha256(blob).hexdigest()))
    lines.append('#endif')
    (root/'generated/cnn/programs.h').write_bytes(('\n'.join(lines)+'\n').encode('utf-8'))
    (root/'kernel/cnn/manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode('utf-8'))
    print('FOUR_FROZEN_PROGRAMS_GENERATED_NOT_GPU')
if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--root',required=True); generate(parser.parse_args().root)
