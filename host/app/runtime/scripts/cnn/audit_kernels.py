"""Identity, control-flow and encoding audit; never a SIMT proof."""
import hashlib
import struct
from generate_kernels import CODE, PRELUDE, FINISH, generate_program, program_words
def audit_program(program_id,blob,args_fixture):
    if bytes(blob)!=generate_program(program_id): raise ValueError('Frozen identity/zero tail mismatch')
    if len(args_fixture)!=16 or args_fixture[:4]!=[0x434e4e31,64,1,program_id]: raise ValueError('Argument identity')
    words=program_words(program_id)
    if words[:8]!=PRELUDE or words[-3:]!=FINISH: raise ValueError('Launch/finish')
    branches=[]; loads=stores=splits=joins=0
    for i,w in enumerate(words):
        op=w&127; f=w>>12&7
        if op==0x63:
            raw=((w>>31)<<12)|((w>>7&1)<<11)|((w>>25&63)<<5)|((w>>8&15)<<1)
            off=raw-8192 if raw&4096 else raw; target=i+off//4
            if off%4 or not i<target<len(words) or (words[target]&0x707f)!=0x300b: raise ValueError('Invalid JOIN branch target')
            branches.append(dict(pc=CODE+i*4,target=CODE+target*4))
        elif op==3:
            if f!=2: raise ValueError('Non-word load')
            loads+=1
        elif op==0x23:
            if f!=2: raise ValueError('Non-word store')
            stores+=1
        elif op==0xb:
            if f==2: splits+=1
            elif f==3: joins+=1
            elif f not in (0,1): raise ValueError('Unsupported SIMT instruction')
        elif op not in (0x13,0x33,0x37,0x73,0xf,0x6f): raise ValueError('Unsupported encoding')
    if splits!=joins or splits!=len(branches): raise ValueError('Unbalanced tail control')
    return dict(scope='STATIC_PROGRAM_AUDIT_NOT_SIMT',id=program_id,sha256=hashlib.sha256(blob).hexdigest(),
                bytes=len(blob),instruction_bytes=len(words)*4,branches=branches,loads=loads,stores=stores,
                split_join_pairs=splits,crt=False,entry=CODE,common_entry=CODE+16)
