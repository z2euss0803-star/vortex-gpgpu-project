"""Serial RV32 integer interpreter. SPLIT/JOIN are decoded, NOT simulated."""
import json
import struct
def signed(v): return (v&0x7fffffff)-(v&0x80000000)
def sext(v,b): return v-(1<<b) if v&(1<<(b-1)) else v
class Memory:
    def __init__(self): self.words={}; self.objects={}; self.expected={}; self.read_ranges=[]; self.write_range=(0,0)
    @classmethod
    def fixture(cls,root):
        m=cls(); manifest=json.loads((root/'manifest.json').read_text())
        for i,t in enumerate(manifest['tensors']):
            base=0x80000000+i*0x1000; blob=(root/t['file']).read_bytes()
            m.objects[t['name']]=(base,t['elements'],blob)
            m.expected[t['name']]=(root/t['expected_file']).read_bytes()
            for j,w in enumerate(struct.unpack('<'+'I'*(len(blob)//4),blob)): m.words[base+j*4]=w
        return m
    def args(self,pid,names):
        addresses=[]; counts=[]; self.read_ranges=[]
        for name in names:
            if name is None: addresses.append(0); counts.append(0)
            else:
                base,count,_=self.objects[name]; addresses.append(base+64); counts.append(count)
        self.read_ranges=[(a,a+4*n) for a,n in zip(addresses[:3],counts[:3]) if n]
        self.write_range=(addresses[3],addresses[3]+4*counts[3])
        return [0x434e4e31,64,1,pid]+addresses+counts+[0]*4
    def read(self,addr):
        if addr%4 or not any(lo<=addr and addr+4<=hi for lo,hi in self.read_ranges): raise ValueError(f'Bad load {addr:#x}')
        return self.words[addr]
    def write(self,addr,value):
        lo,hi=self.write_range
        if addr%4 or not lo<=addr or addr+4>hi: raise ValueError(f'Bad store {addr:#x}')
        self.words[addr]=value&0xffffffff
    def verify(self,name):
        base,_,blob=self.objects[name]
        actual=struct.pack('<'+'I'*(len(blob)//4),*(self.words[base+i] for i in range(0,len(blob),4)))
        assert actual==self.expected[name], f'{name} numerical/guard mismatch'
    def verify_guards(self):
        for base,n,blob in self.objects.values():
            for i in range(0,len(blob),4):
                if 64<=i<64+n*4: continue
                assert self.words[base+i]==struct.unpack_from('<I',blob,i)[0], 'Guard/padding changed'

def run_program(blob,args_words,memory,logical_tid,max_steps=2000):
    if len(blob)!=4096 or not 0<=logical_tid<64: raise ValueError('Model input')
    words=struct.unpack('<1024I',blob); r=[0]*32; pc=0
    trace=dict(scope='CPU_RV32_MODEL_NOT_SIMT',writes=[],loads=[],fence=False,stopped=False,tid=logical_tid)
    for step in range(max_steps):
        if pc%4 or not 0<=pc<len(blob): raise ValueError('Instruction address')
        w=words[pc//4]; op=w&127; rd=w>>7&31; f=w>>12&7; a=w>>15&31; b=w>>20&31; f7=w>>25; nxt=pc+4; v=None
        if op==0x37: v=w&0xfffff000
        elif op==0x13:
            n=sext(w>>20,12)
            if f==0: v=r[a]+n
            elif f==1: v=r[a]<<(b&31)
            elif f==3: v=int(r[a]<(n&0xffffffff))
            elif f==4: v=r[a]^n
            elif f==5: v=signed(r[a])>>(b&31) if f7==0x20 else r[a]>>(b&31)
            else: raise ValueError('Immediate opcode')
        elif op==0x33:
            if f7==1:
                if f==0: v=r[a]*r[b]
                elif f==5: v=r[a]//r[b] if r[b] else 0xffffffff
                elif f==7: v=r[a]%r[b] if r[b] else r[a]
                else: raise ValueError('M opcode')
            elif f==0: v=r[a]-r[b] if f7==0x20 else r[a]+r[b]
            elif f==2: v=int(signed(r[a])<signed(r[b]))
            elif f==4: v=r[a]^r[b]
            elif f==7: v=r[a]&r[b]
            else: raise ValueError('Register opcode')
        elif op==3 and f==2:
            addr=(r[a]+sext(w>>20,12))&0xffffffff
            if 0x10010000<=addr<0x10010040 and addr%4==0: v=args_words[(addr-0x10010000)//4]
            else: v=memory.read(addr)
            trace['loads'].append(addr)
        elif op==0x23 and f==2:
            addr=(r[a]+sext(((w>>25)<<5)|(w>>7&31),12))&0xffffffff
            memory.write(addr,r[b]); trace['writes'].append(addr)
        elif op==0x63 and f==0:
            n=((w>>31)<<12)|((w>>7&1)<<11)|((w>>25&63)<<5)|((w>>8&15)<<1)
            if r[a]==r[b]: nxt=pc+sext(n,13)
        elif op==0x73 and f==2 and a==0:
            csr=w>>20
            if csr==0x340: v=0x10010000
            elif csr==0xf14: v=logical_tid
            else: raise ValueError('CSR not supported')
        elif op==0xb:
            if f==0 and a==0:
                trace['stopped']=True; return trace
            if f==2: v=0 # serial token placeholder, not SIMT semantics
            elif f not in (0,1,3): raise ValueError('SIMT opcode not supported')
        elif w==0x0330000f: trace['fence']=True
        else: raise ValueError(f'Unsupported instruction {w:#x}')
        if v is not None and rd: r[rd]=v&0xffffffff
        r[0]=0; pc=nxt
    raise ValueError('Instruction budget exceeded')
