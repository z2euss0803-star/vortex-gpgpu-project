"""Private IMAGE namespace catalogs; legacy raw words stay byte-identical.

The original START contains a hardcoded spawn PC. These programs must execute
at0x10000000 and replace each other ONLY after verified ACK. Table references
are GPU-read-only views of one owner-uploaded immutable64KiB DATA slot (not a
claim of a kernel-sealed public object). Private param2/3 carry checked table
suboffsets; static word decoding proves the frozen shaders never read them.
"""
import hashlib,json,pathlib,struct
from scripts.unified.build_catalog import build_catalog

ABSENT=0xffffffff
def schema(op):
    fields=[]
    def scalar(offset,lo,hi):
        f=[0]*16;f[0]=1;f[1]=offset;f[11]=lo;f[12]=hi;fields.append(f)
    def zero(offset):
        f=[0]*16;f[0]=3;f[1]=offset;f[10]=4;fields.append(f)
    def address(offset,ref,access,pixels=False,size=0,relative=ABSENT,table_gauss=False):
        f=[0]*16;f[:10]=[2,offset,ref,access,4,4 if pixels or table_gauss else 0,16 if pixels else 48 if table_gauss else ABSENT,20 if pixels else ABSENT,ABSENT,relative];f[10]=size;fields.append(f)
    address(0,0,1,pixels=True)
    if op in (5,7):address(4,1,1,pixels=True)
    elif op==9:address(4,4,1,size=64,relative=56)
    else:zero(4)
    if op==7:address(8,2,1,pixels=True)
    else:zero(8)
    address(12,3,2,pixels=True)
    for offset,lo,hi in [(16,256,1280),(20,144,720),(24,0,1279),(28,0,719),(32,1,1280),(36,1,720)]:scalar(offset,lo,hi)
    if op in (3,4):address(40,4,1,relative=52,table_gauss=True)
    elif op in (2,5,7,9):address(40,4,1,size={2:1024,5:16384,7:32,9:32}[op],relative=52)
    else:zero(40)
    scalar(44,3 if op in (3,4) else 2 if op in (8,9) else 0,4 if op in (3,4) else 8 if op in (8,9) else ABSENT)
    scalar(48,7 if op in (3,4) else 0,9 if op in (3,4) else ABSENT)
    subrange={2:(0,6144),3:(7488,7552),4:(7488,7552),5:(8192,8192),7:(7680,7872),9:(7168,7360)}.get(op,(0,0))
    scalar(52,*subrange);scalar(56,7424 if op==9 else 0,7424 if op==9 else 0);scalar(60,0,1)
    def predicate(operation,lhs=ABSENT,rhs=ABSENT,aux=ABSENT,parameters=()):
        values=[4,lhs,operation,rhs,aux]+list(parameters)+[0]*(8-len(parameters))+[0]*3
        assert len(values)==16;fields.append(values)
    # Generic schema2 relations; no model/image dispatch in the kernel.
    predicate(2,16,20,parameters=(256,144,1280,720))
    predicate(1,24,32,16);predicate(1,28,36,20)
    predicate(5,parameters=(0,3))
    if op in (5,7):predicate(5,parameters=(1,3))
    if op==7:predicate(5,parameters=(2,3))
    if op in (3,4):
        predicate(2,44,48,parameters=(3,7,4,9))
        predicate(2,44,52,parameters=(3,7488,4,7552))
    if op in (8,9):predicate(3,44,parameters=(2,4,8))
    if op in (2,7,9):predicate(4,52,parameters={2:(0,1024,7),7:(7680,32,7),9:(7168,32,7)}[op])
    h=[0x534c5856,2,64+64*len(fields),64,len(fields),0x10000000,0,0,0,0,0,1,0,0,0,0]
    return struct.pack('<16I',*h)+b''.join(struct.pack('<16I',*f) for f in fields)

def image_programs(root,provider):
    root=pathlib.Path(root)
    if provider.get('name')!='S8_NATIVE_INT16' or provider.get('provider_flags')!=1 or provider.get('namespaces')!=[1,3]:raise ValueError('EXACT_S8_IMAGE_PROVIDER')
    manifest_raw=(root/'gpu_programs/manifest.json').read_bytes()
    if hashlib.sha256(manifest_raw).hexdigest()!='3291001ec8f88edcfd64688a2bb88f8c4ea1592455e0b6a47d6b202a9d254fdd':raise ValueError('ORIGINAL_MANIFEST_SHA')
    manifest=json.loads(manifest_raw)
    if manifest['start_PC']!='0x10000000' or manifest['hart_stride']!=64 or len(manifest['kernels'])!=10:raise ValueError('ORIGINAL_IMAGE_EXECUTION_GEOMETRY')
    table=(root/'gpu_programs/tables-64KiB.bin').read_bytes()
    if len(table)!=65536 or hashlib.sha256(table).hexdigest()!=manifest['table_payload_sha256']:raise ValueError('ORIGINAL_READ_TABLE')
    records=[]
    for op,gold in enumerate(manifest['kernels']):
        raw=(root/'gpu_programs'/(gold['name']+'.bin')).read_bytes();padded=(root/'gpu_programs'/(gold['name']+'.padded.bin')).read_bytes()
        if gold['op']!=op or len(raw)!=gold['bytes'] or len(padded)!=gold['padded_bytes'] or hashlib.sha256(raw).hexdigest()!=gold['sha256'] or hashlib.sha256(padded).hexdigest()!=gold['padded_sha256']:raise ValueError('ORIGINAL_WORD_SHA')
        if len(padded)>2048 or padded!=raw+struct.pack('<I',0x00100073)*((len(padded)-len(raw))//4):raise ValueError('ORIGINAL_EBREAK_PADDING')
        # LW from argument register x2 at52/56 would invalidate our private
        # relative-offset transport. Never guess these fields are unused.
        words=struct.unpack('<%uI'%(len(raw)//4),raw)
        if any(w&0x7f==3 and (w>>15)&31==2 and w>>20 in (52,56) for w in words):raise ValueError('PARAM2_3_ARE_NOT_UNUSED')
        code=padded+bytes(2048-len(padded));sealed=schema(op)
        fields=[3,256,512+op,0,0,2048,2048,0,0x10000000,0x10010000,64,0,len(sealed),(len(sealed)-64)//64,1,0]
        digests=[hashlib.sha256(code).digest(),hashlib.sha256(b'').digest(),hashlib.sha256(sealed).digest(),bytes.fromhex(provider['hardware_contract']),bytes.fromhex(provider['bit_sha256']),bytes.fromhex(provider['xsa_sha256']),bytes(32),hashlib.sha256(raw).digest()]
        records.append(dict(fields=fields,digests=digests,schema=sealed,code=code,name=gold['name'],padded_sha256=gold['padded_sha256']))
    return records

def write_assets(out):
    root=pathlib.Path(__file__).resolve().parents[2];provider=json.loads((root/'contracts/unified/providers.json').read_bytes())['providers'][0]
    records=image_programs(root/'vendor/image-studio',provider);out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False)
    catalog=build_catalog(records);(out/'catalog.bin').write_bytes(catalog);rows=[]
    for i,r in enumerate(records):
        entry=struct.unpack_from('<16I',catalog,64+512*i);eid=catalog[64+512*i+256:64+512*i+288].hex()
        (out/(eid+'.bin')).write_bytes(r['code']);(out/(r['name']+'.schema.bin')).write_bytes(r['schema'])
        rows.append(dict(op=i,name=r['name'],entry_id=eid,logical_profile=256,logical_program=512+i,code_sha256=r['digests'][0].hex(),raw_sha256=r['digests'][7].hex(),padded_sha256=r['padded_sha256'],schema_sha256=r['digests'][2].hex(),schema_offset=entry[11],gpu_raw_cycles=None))
    result=dict(scope='STATIC_IMAGE_PROGRAM_BINDINGS_NOT_GPU_PASS',provider=provider,programs=rows,catalog_sha256=hashlib.sha256(catalog).hexdigest(),gpu_raw_cycles=None)
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    import sys
    print(json.dumps(write_assets(sys.argv[1])))
