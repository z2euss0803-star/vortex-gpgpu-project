"""MS8AST1 LE native INT16 assets. Standard-library-only; no recalibration.

The external manifest is trusted input from the frozen catalog, not supplied by
an untrusted caller together with an arbitrary blob. All semantic checks still
run after cryptographic identity checks. This is not a GPU/runtime admission.
"""
import argparse,hashlib,json,math,pathlib,struct

MAGIC=b'MS8AST1\0';HEADER=256;LAYER=384;COUNT=16;PAYLOAD=HEADER+LAYER*COUNT
MAX_BYTES=16*1024*1024
SOURCE_KEYS=('model','selected','source_old','checkpoint')
FIELDS=(('q16',1,1),('coeff',2,2),('sA',3,2),('norm_gamma',4,2),('norm_beta',5,2))

def sha(data):return hashlib.sha256(data).hexdigest()
def align(n):return (n+63)//64*64
def checked_hex(value):
    if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value):raise ValueError('SHA256 encoding')
    return bytes.fromhex(value)
def shape(d):
    co,ci,k,norm=(d[x] for x in ('co','ci','k','norm'))
    if any(type(v) is not int for v in (co,ci,k)) or not 1<=co<=128 or not 1<=ci<=128 or k not in (3,9) or norm not in (0,1):raise ValueError('layer geometry')
    inner=ci*k*k
    if inner>4096:raise ValueError('model K exceeds hardware limit')
    return co,ci,k,int(norm),inner,align(inner*2)//2
def geometry(d):return {key:int(d[key]) for key in ('co','ci','k','norm')}
def required(d):return FIELDS if d['norm'] else FIELDS[:3]
def dims(name,co,kpad):return (co,kpad) if name=='q16' else (co,2) if name=='coeff' else (1,1) if name=='sA' else (co,1)
def check_value(name,data,co,inner,kpad):
    if name=='q16':
        for i,(q,) in enumerate(struct.iter_unpack('<h',data)):
            if q==-32768 or (i%kpad>=inner and q!=0):raise ValueError('production q16 or padding')
    else:
        for i,(v,) in enumerate(struct.iter_unpack('<f',data)):
            if not math.isfinite(v) or ((name=='sA' or name=='coeff' and i%2==0) and v<=0):raise ValueError('coefficient finite/scale')

def build_assets(layers,sources):
    if len(layers)!=COUNT or set(sources)!=set(SOURCE_KEYS):raise ValueError('sixteen layers and frozen source identities required')
    identities=[checked_hex(sources[k]) for k in SOURCE_KEYS]
    data=bytearray(PAYLOAD);records=[]
    for i,d in enumerate(layers):
        co,ci,k,norm,inner,kpad=shape(d)
        if set(d)!={'co','ci','k','norm'}|{f[0] for f in required(d)}:raise ValueError('unexpected layer fields')
        start=HEADER+i*LAYER;struct.pack_into('<8I',data,start,i,co,ci,k,inner,kpad,norm,len(required(d)))
        for j,(name,kind,dtype) in enumerate(required(d)):
            raw=bytes(d[name]);rows,cols=dims(name,co,kpad)
            if len(raw)!=rows*cols*(2 if dtype==1 else 4):raise ValueError('segment size')
            check_value(name,raw,co,inner,kpad)
            data.extend(bytes(align(len(data))-len(data)));offset=len(data);data.extend(raw)
            if len(data)>MAX_BYTES:raise ValueError('weight slot capacity')
            entry=start+64+j*64
            struct.pack_into('<IIQQII32s',data,entry,kind,dtype,offset,len(raw),rows,cols,hashlib.sha256(raw).digest())
        records.append(geometry(d))
    data.extend(bytes(align(len(data))-len(data)))
    struct.pack_into('<8sIIIIQQ',data,0,MAGIC,1,HEADER,COUNT,LAYER,len(data),PAYLOAD)
    for i,value in enumerate(identities):data[48+i*32:80+i*32]=value
    data[176:208]=hashlib.sha256(data[HEADER:]).digest()
    blob=bytes(data)
    manifest=dict(version=1,format='MS8AST1',bytes=len(blob),sha256=sha(blob),source_sha256=dict(sources),layers=records,
                  scope='NATIVE_INT16_ASSETS_NOT_NUMERIC_OR_GPU_PASS',gpu_raw_cycles=None)
    load_assets(blob,manifest)
    return blob,manifest

def load_assets(blob,manifest):
    blob=bytes(blob)
    if not isinstance(manifest,dict) or manifest.get('version')!=1 or manifest.get('format')!='MS8AST1':raise ValueError('trusted asset manifest')
    if not PAYLOAD<=len(blob)<=MAX_BYTES or len(blob)%64 or manifest.get('bytes')!=len(blob):raise ValueError('package length')
    if sha(blob)!=manifest.get('sha256'):raise ValueError('external package SHA')
    if struct.unpack_from('<8sIIIIQQ',blob)!=(MAGIC,1,HEADER,COUNT,LAYER,len(blob),PAYLOAD):raise ValueError('header endian/version/layout')
    if any(blob[40:48]) or any(blob[208:256]) or blob[176:208]!=hashlib.sha256(blob[256:]).digest():raise ValueError('reserved/body SHA')
    sources=manifest.get('source_sha256',{})
    if set(sources)!=set(SOURCE_KEYS):raise ValueError('source identity set')
    for i,name in enumerate(SOURCE_KEYS):
        if blob[48+i*32:80+i*32]!=checked_hex(sources[name]):raise ValueError('source identity mismatch')
    expected=manifest.get('layers')
    if not isinstance(expected,list) or len(expected)!=COUNT:raise ValueError('layer count')
    cursor=PAYLOAD;result=[]
    for i,d in enumerate(expected):
        co,ci,k,norm,inner,kpad=shape(d);start=HEADER+i*LAYER
        if struct.unpack_from('<8I',blob,start)!=(i,co,ci,k,inner,kpad,norm,len(required(d))) or any(blob[start+32:start+64]):raise ValueError('layer geometry/order/reserved')
        decoded=geometry(d)
        for j,(name,kind,dtype) in enumerate(required(d)):
            entry=start+64+j*64
            actual_kind,actual_type,offset,size,rows,cols,digest=struct.unpack_from('<IIQQII32s',blob,entry)
            dims_expected=dims(name,co,kpad);size_expected=dims_expected[0]*dims_expected[1]*(2 if dtype==1 else 4)
            if (actual_kind,actual_type,rows,cols)!=(kind,dtype,*dims_expected) or size!=size_expected:raise ValueError('segment kind/type/shape/length')
            if offset!=align(cursor) or offset>len(blob) or size>len(blob)-offset or any(blob[cursor:offset]):raise ValueError('segment range/alignment/overlap/padding')
            raw=blob[offset:offset+size]
            if hashlib.sha256(raw).digest()!=digest:raise ValueError('segment SHA')
            check_value(name,raw,co,inner,kpad);decoded[name]=raw;cursor=offset+size
        used=start+64+len(required(d))*64
        if any(blob[used:start+LAYER]):raise ValueError('unused descriptor')
        result.append(decoded)
    if align(cursor)!=len(blob) or any(blob[cursor:]):raise ValueError('trailing data/padding')
    return result

def export_assets(source_manifest,out):
    from scripts.demo.identity import verify_sources,sha256_file
    source_manifest=pathlib.Path(source_manifest);out=pathlib.Path(out)
    if not source_manifest.is_file():raise ValueError('missing frozen source manifest')
    verify_sources(source_manifest)
    source=json.loads(source_manifest.read_text(encoding='utf-8'));root=pathlib.Path(source['roots']['mosaic'])
    selected=json.loads((root/'config/selected_candidate.json').read_text(encoding='utf-8'))
    model_path=root/'inputs/model.json';model=json.loads(model_path.read_text(encoding='utf-8'))
    old_path=root/'artifacts/selected_weights_device.bin';old=old_path.read_bytes()
    meta=json.loads(old_path.with_suffix('.json').read_text(encoding='utf-8'))
    if (selected['candidate']!='w16a16_calibration_mse_v2' or meta['candidate']!=selected['candidate'] or
        len(selected['activation_scales'])!=16 or len(model['layers'])!=16 or len(meta['layers'])!=16 or
        struct.unpack_from('<8sIIIIQ',old)!=(b'W16APKG\0',1,32,16,0,len(old)) or sha(old)!=meta['sha256']):raise ValueError('frozen selected source')
    layers=[]
    for i,(definition,m) in enumerate(zip(model['layers'],meta['layers'])):
        d={key:int(definition[key]) for key in ('co','ci','k','norm')};co,ci,k,norm,inner,kpad=shape(d)
        if m['logical_shape']!=[co,ci,k,k] or m['name']!=definition['name']:raise ValueError('source topology')
        segments={}
        for s in m['segments']:
            offset,size=s['offset'],s['bytes']
            if s['name'] in segments or offset<32 or offset%64 or offset>len(old) or size<=0 or size>len(old)-offset:raise ValueError('source segment range')
            raw=old[offset:offset+size]
            if sha(raw)!=s['sha256']:raise ValueError('source segment SHA')
            segments[s['name']]=raw
        if len(segments['high'])!=co*inner or len(segments['low'])!=co*inner:raise ValueError('source q16 size')
        q=bytearray(co*kpad*2)
        for pos,(hi,lo) in enumerate(zip(segments['high'],segments['low'])):
            value=(hi if hi<128 else hi-256)*256+lo
            struct.pack_into('<h',q,2*((pos//inner)*kpad+pos%inner),value)
        d['q16']=bytes(q)
        if len(segments['scale'])!=co*4 or len(segments['bias'])!=co*4:raise ValueError('source coefficient size')
        d['coeff']=b''.join(segments['scale'][c*4:c*4+4]+segments['bias'][c*4:c*4+4] for c in range(co))
        d['sA']=struct.pack('<f',selected['activation_scales'][i])
        if norm:
            d['norm_gamma']=segments['norm_gamma'];d['norm_beta']=segments['norm_beta']
        layers.append(d)
    identities=dict(model=sha256_file(model_path),selected=sha256_file(root/'config/selected_candidate.json'),source_old=sha(old),checkpoint=sha256_file(root/'inputs/mosaic.pth'))
    blob,manifest=build_assets(layers,identities)
    before=sha256_file(source_manifest)
    verify_sources(source_manifest)
    if before!=sha256_file(source_manifest):raise ValueError('source manifest changed during export')
    # Only create output after all source/semantic checks; never replace old runs.
    out.mkdir(parents=True,exist_ok=False)
    with (out/'weights.ms8').open('wb') as f:
        for start in range(0,len(blob),262144):f.write(blob[start:start+262144])
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (out/'model-v1.json').write_text(json.dumps(dict(version=1,layers=model['layers'],sources=identities),indent=2)+'\n',encoding='utf-8')
    if sha256_file(out/'weights.ms8')!=manifest['sha256']:raise ValueError('written asset differs')
    return manifest

def write_c_manifest(manifest,path):
    def array(h):return '{'+','.join('0x'+h[i:i+2] for i in range(0,64,2))+'}'
    text='#include <vxl/mosaic_assets.h>\nstatic const struct mosaic_manifest fixture_manifest={\n'
    text+=' .package_sha='+array(manifest['sha256'])+',\n .source_sha={'+','.join(array(manifest['source_sha256'][k]) for k in SOURCE_KEYS)+'},\n .layers={\n'
    text+=',\n'.join('  {'+','.join(str(d[k]) for k in ('co','ci','k','norm'))+'}' for d in manifest['layers'])+'\n }\n};\n'
    pathlib.Path(path).write_text(text,encoding='utf-8')

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest');p.add_argument('--out',required=True);p.add_argument('--test-fixture',action='store_true');a=p.parse_args()
    if a.test_fixture:
        from tests.mosaic_s8.test_assets import layers,SOURCES
        out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=False);blob,m=build_assets(layers(),SOURCES);(out/'weights.ms8').write_bytes(blob);write_c_manifest(m,out/'mosaic_fixture.h')
    else:
        m=export_assets(a.manifest,a.out);write_c_manifest(m,pathlib.Path(a.out)/'mosaic_fixture.h')
    print(json.dumps(dict(scope=m['scope'],bytes=m['bytes'],sha256=m['sha256'],gpu_raw_cycles=None)))

if __name__=='__main__':main()
