"""Frozen W4/P8 namespace2 programs9..12. No old NN fastwait policy."""
import hashlib,json,pathlib,struct,sys
from scripts.unified.build_catalog import build_catalog
ABSENT=0xffffffff
def schema(program):
    high=program>=11;transform=program in (9,11);fields=[]
    def scalar(off,lo,hi,n=0):
        f=[0]*16;f[0]=1;f[1]=off;f[10]=n;f[11]=lo;f[12]=hi;fields.append(f)
    def address(off,ref,access,size):
        f=[2,off,ref,access,4,0,ABSENT,ABSENT,ABSENT,ABSENT,size,0,0,0,0,0];fields.append(f)
    def zero(off,n):
        f=[0]*16;f[0]=3;f[1]=off;f[10]=n;fields.append(f)
    def disjoint(a,b):fields.append([4,ABSENT,5,ABSENT,ABSENT,a,b]+[0]*9)
    if transform:
        address(0,0,1,514048);address(4,1,2,514048);scalar(8,16064 if high else 448,16064 if high else 448);scalar(12,0,ABSENT,48);zero(60,4);disjoint(0,1)
    else:
        sizes=[1048576 if high else 262144,1048576 if high else 262144,8388608,131072 if high else 32768,16777216 if high else 1048576,1048576]
        for i,size in enumerate(sizes):address(i*4,i,2 if i<2 else 1,size)
        scalar(24,0xffece4dc,0xffece4dc);scalar(28,0,1);scalar(32,30926 if high else 648,30926 if high else 768);address(36,6,1,1048576 if high else 262144);zero(40,24)
        # All indirect source ranges remain private immutable owner inputs.
        # No writable result may alias another result or a read source.
        for a in range(2):
            for b in range(a+1,7):disjoint(a,b)
    header=[0x534c5856,2,64+64*len(fields),64,len(fields),0x10000000,0,0,0,0,0,2,0,0,0,0]
    return struct.pack('<16I',*header)+b''.join(struct.pack('<16I',*f) for f in fields)
def write_assets(out):
    root=pathlib.Path(__file__).resolve().parents[2];provider=json.loads((root/'contracts/unified/providers.json').read_bytes())['providers'][1]
    assert provider['name']=='P8_W4_GRAPHICS' and provider['provider_flags']==2 and provider['namespaces']==[2]
    out=pathlib.Path(out);out.mkdir(exist_ok=False);entries=[]
    for program,name in [(9,'cm_transform_kernel'),(10,'cm_raster_kernel'),(11,'cm_transform_high_kernel'),(12,'cm_raster_high_kernel')]:
        raw=(root/'data/p8'/(name+'.bin')).read_bytes();assert 0<len(raw)<2048 and len(raw)%4==0
        code=raw+bytes(2048-len(raw));sealed=schema(program)
        fields=[2,5,program,0,0,2048,2048,0,0x10000000,0x10010000,64,0,len(sealed),(len(sealed)-64)//64,2,0]
        digests=[hashlib.sha256(code).digest(),hashlib.sha256(b'').digest(),hashlib.sha256(sealed).digest(),bytes.fromhex(provider['hardware_contract']),bytes.fromhex(provider['bit_sha256']),bytes.fromhex(provider['xsa_sha256']),bytes(32),hashlib.sha256(raw).digest()]
        entries.append(dict(fields=fields,digests=digests,schema=sealed,code=code,name=name,program=program))
    catalog=build_catalog(entries);(out/'catalog.bin').write_bytes(catalog);rows=[]
    for i,r in enumerate(entries):
        entry=catalog[64+512*i:64+512*(i+1)];eid=entry[256:288].hex();(out/(eid+'.bin')).write_bytes(r['code']);(out/(r['name']+'.schema.bin')).write_bytes(r['schema'])
        rows.append(dict(logical_program=r['program'],name=r['name'],entry_id=eid,code_sha256=r['digests'][0].hex(),schema_sha256=r['digests'][2].hex(),raw_sha256=r['digests'][7].hex(),args_bytes=64,data_refs=2 if r['program'] in (9,11) else 7,gpu_raw_cycles=None))
    policies=[]
    for r in rows:
        program=r['logical_program'];high=program>=11;raster=program in (10,12)
        indirect=[3,5,32,16384 if high else 4096,30926 if high else 768,262144] if raster else [ABSENT,ABSENT,0,0,0,0]
        # No synthetic CRT or rewritten code. Original shaders manually sample
        # textures; trusted TEX descriptor is deliberately disabled on each job.
        values=[4000000000,ABSENT,0,0,0,0]+[0]*6+indirect+[0]+[0]*20+[1]+[0]*16
        policy=bytes.fromhex(r['entry_id'])+struct.pack('<56I',*values);assert len(policy)==256;policies.append(policy)
    payload=b''.join(policies);execution=struct.pack('<4I',0x454c5856,2,64+len(payload),len(policies))+hashlib.sha256(catalog).digest()+bytes(16)+payload
    (out/'execution-v2.bin').write_bytes(execution)
    record=dict(scope='PRIVATE_CATALOG_NOT_GPU',namespace=2,logical_profile=5,provider=provider,catalog_sha256=hashlib.sha256(catalog).hexdigest(),programs=rows,execution_policy=dict(version=2,bytes=len(execution),sha256=hashlib.sha256(execution).hexdigest(),retirement='GLOBAL_DRAIN_NO_EXIT_WORDS',normalized_core_mask='GLOBAL_RETIREMENT_INFERENCE_NOT_RAW_CORE_MMIO',tex='DISABLED_ORIGINAL_MANUAL_SAMPLER',board='NOT_RUN'),gpu_raw_cycles=None);(out/'manifest.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
    lines=['#pragma once','static constexpr unsigned p8_program_ids[]={9,10,11,12};']
    for name,key in [('code','code_sha256'),('schema','schema_sha256')]:
        lines.append('static constexpr unsigned char p8_'+name+'_sha[4][32]={')
        for r in rows:lines.append('{'+','.join(str(x) for x in bytes.fromhex(r[key]))+'},')
        lines.append('};')
    (root/'generated/p8/programs.h').write_bytes(('\n'.join(lines)+'\n').encode());return record
if __name__=='__main__':print(json.dumps(write_assets(sys.argv[1])))
