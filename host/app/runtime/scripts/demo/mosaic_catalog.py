"""Acyclic private program12 catalog; no GPU or target numeric admission."""
import hashlib,struct
from scripts.demo.mosaic_rv32_contract import validate_runtime
from scripts.mosaic.contract import tagged
from scripts.mosaic.layout import check_layout
from scripts.mosaic.assets import SOURCE_KEYS
def mutable_ranges(sections,runtime):
    """ELF TLS template overlaps .got in frozen CRT; allocated TLS is at _end.

    Never exempt the overlapping SHF_TLS template or any executable/read-only
    section from integrity checking. Only actual writable globals and the
    separately allocated 64-hart TLS arena are mutable.
    """
    validate_runtime(dict(runtime));spans=[]
    for s in sections:
        if not s['flags']&2 or not s['flags']&1 or s['flags']&0x400 or not s['bytes']:continue
        if s['flags']&4:raise ValueError('writable executable ELF section')
        lo=s['address']-0x80000000;hi=lo+s['bytes']
        if lo<4 or lo%4 or hi%4 or hi>runtime['tls_base']-0x80000000:raise ValueError('writable ELF extent')
        spans.append((lo,hi))
    base=runtime['tls_base']-0x80000000;spans.append((base,base+64*runtime['tbss_stride']))
    result=[]
    for lo,hi in sorted(spans):
        if result and lo<=result[-1]['offset']+result[-1]['bytes']:
            r=result[-1];r['bytes']=max(r['offset']+r['bytes'],hi)-r['offset']
        else:result.append(dict(offset=lo,bytes=hi-lo))
    if len(result)>64:raise ValueError('mutable span count')
    return result
def digest(value):
    if type(value)!=str or len(value)!=64 or any(c not in '0123456789abcdef' for c in value) or value=='0'*64:raise ValueError('nonzero SHA256')
    return bytes.fromhex(value)
def catalog_digests(rows,policy):
    if not 1<=len(rows)<=32:raise ValueError('catalog count')
    layouts=struct.pack('<4I',0x314c4d56,1,len(rows),296)
    graphs=struct.pack('<4I',0x31474d56,1,len(rows),32)+digest(policy);seen=set();tags=set()
    for row in rows:
        g,l=row['graph'],row['layout'];check_layout(l)
        raw=dict(g);expected=raw.pop('sha256',None)
        if tagged(raw)!=expected or l['graph_sha256']!=expected or (g['width'],g['height'])!=(l['width'],l['height']):raise ValueError('actual graph/layout identity')
        shape=(l['width'],l['height'])
        for value in (g['sha256'],l['sha256']):
            if value[:32] in tags:raise ValueError('ambiguous device tag')
            tags.add(value[:32])
        if shape in seen:raise ValueError('duplicate geometry')
        seen.add(shape);encoded=struct.pack('<II',*shape)
        for r in l['regions']:encoded+=struct.pack('<QQII',r['address'],r['bytes'],r['guard'],int(r['readonly']))
        encoded+=struct.pack('<Q',l['end']);assert len(encoded)==232
        layouts+=encoded+digest(g['sha256'])+digest(l['sha256'])
        graphs+=struct.pack('<II',*shape)+digest(g['sha256'])
    return hashlib.sha256(layouts).hexdigest(),hashlib.sha256(graphs).hexdigest()
def runtime_digest(runtime,mutable,deps,ro_sha):
    p=validate_runtime(dict(runtime))
    if not 1<=len(mutable)<=64:raise ValueError('mutable count')
    fields=[p[k] for k in ('crt_base','crt_bytes','tls_base','tbss_bytes','tbss_stride','tbss_align','stack_stride','static_stack_bytes')]
    raw=struct.pack('<4I',0x31524d56,1,p['runtime_bytes'],len(mutable))+struct.pack('<8I',*fields)+struct.pack('<QQ',p['stack_base'],p['stack_bytes'])+digest(deps)
    end=4
    for r in mutable:
        offset,size=r['offset'],r['bytes']
        if type(offset)!=int or type(size)!=int or offset<end or not size or (offset|size)&3 or offset+size>p['runtime_bytes']:raise ValueError('mutable extent/overlap')
        end=offset+size;raw+=struct.pack('<II',offset,size)
    raw+=struct.pack('<III',0x31524f4d,p['rodata_gpu'],p['rodata_bytes'])+digest(ro_sha)
    return hashlib.sha256(raw).hexdigest()
def make_template(receipt,rows,manifest,mutable,policy,deps,source):
    if receipt.get('natural_exit')!=0 or receipt.get('scope')!='ACTUAL_MOSAIC_RV32_LINK_NOT_EXECUTED':raise ValueError('actual RV32 receipt required')
    runtime=validate_runtime(dict(receipt['runtime']));flat=receipt['flat'];ro=receipt['rodata']
    if not 4<=flat['bytes']<=runtime['runtime_bytes'] or flat['bytes']%64 or ro['bytes']!=runtime['rodata_bytes'] or ro['gpu']!=runtime['rodata_gpu']:raise ValueError('compiled flat/sidecar extent')
    if not 6400<=manifest['bytes']<=16777216 or manifest['bytes']%64 or len(manifest['layers'])!=16:raise ValueError('native asset extent')
    src=manifest['source_sha256']
    if set(src)!=set(SOURCE_KEYS):raise ValueError('frozen source identities')
    for value in src.values():digest(value)
    for row in rows:
        g=row['graph']
        if g['sources']!=src or g['asset_bytes']!=manifest['bytes'] or [{k:int(x[k]) for k in ('co','ci','k','norm')} for x in g['layers']]!=manifest['layers']:raise ValueError('manifest differs from actual frozen graph')
    layout,graph=catalog_digests(rows,policy)
    hashes=dict(device_flat=flat['sha256'],device_elf=receipt['elf']['sha256'],model=src['model'],assets=manifest['sha256'],graph_catalog=graph,layout_catalog=layout,rv32_runtime=runtime_digest(runtime,mutable,deps,ro['sha256']))
    digest(source)
    for value in hashes.values():digest(value)
    return dict(schema=1,profile=6,program=12,source_sha256=source,runtime=runtime,flat=dict(flat),rodata=dict(ro),assets=manifest,layouts=rows,mutable=mutable,graph_policy_sha256=policy,runtime_dependencies_sha256=deps,digests=hashes,gpu_raw_cycles=None,scope='PRIVATE_CATALOG_TEMPLATE_NO_RUNNER_SELF_HASH_OR_BINDING')
def c_array(raw):return '{'+','.join(str(x) for x in raw)+'}'
def template_header(t):
    """C/C++ common metadata. Payload pointers and external binding filled later."""
    array=lambda value:c_array(digest(value))
    text='#ifndef DEMO_MOSAIC_RELEASE_TEMPLATE_H\n#define DEMO_MOSAIC_RELEASE_TEMPLATE_H\n#include <vxl/mosaic_host.h>\n'
    full=[i for i,r in enumerate(t['layouts']) if (r['layout']['width'],r['layout']['height'])==(1280,960)]
    if len(full)!=1:raise ValueError('formal full-resolution default layout missing')
    text+='#define DEMO_RELEASE_MOSAIC_DEFAULT_LAYOUT '+str(full[0])+'u\n'
    m=t['assets'];text+='static const struct mosaic_manifest demo_release_mosaic_assets={'+array(m['sha256'])+',{'+','.join(array(m['source_sha256'][k]) for k in SOURCE_KEYS)+'},{'+','.join('{'+','.join(str(r[k]) for k in ('co','ci','k','norm'))+'}' for r in m['layers'])+'}};\n'
    text+='static const struct mosaic_mutable demo_release_mosaic_mutable[]={'+','.join('{'+str(r['offset'])+','+str(r['bytes'])+'}' for r in t['mutable'])+'};\n'
    entries=[]
    for r in t['layouts']:
        l=r['layout'];region=lambda x:'{'+','.join(str(x[k]) for k in ('address','bytes','guard'))+','+str(int(x['readonly']))+'}'
        entries.append('{{'+str(l['width'])+','+str(l['height'])+',{'+','.join(region(x) for x in l['regions'])+'},'+str(l['end'])+'},'+array(r['graph']['sha256'])+','+array(l['sha256'])+'}')
    text+='static const struct mosaic_catalog_layout demo_release_mosaic_layouts[]={'+','.join(entries)+'};\n'
    # User provider checks this additional anchor; kernel retains the identical
    # digest in its complete immutable512B binding, so no unused duplicate.
    text+='#ifndef __KERNEL__\nstatic const unsigned char demo_release_source_sha[32]='+array(t['source_sha256'])+';\n#endif\n'
    text+='static inline void demo_release_mosaic_template(struct mosaic_program *p){\n memset(p,0,sizeof(*p));\n'
    p=t['runtime']
    for name in ('runtime_bytes','entry','crt_base','crt_bytes','tls_base','tbss_bytes','tbss_stride','tbss_align','stack_stride','static_stack_bytes','stack_base','stack_bytes','rodata_gpu','rodata_bytes'):text+=' p->'+name+'='+str(p[name])+('ULL' if name in ('stack_base','stack_bytes') else 'u')+';\n'
    text+=' p->flat_bytes='+str(t['flat']['bytes'])+';p->mutable_ranges=demo_release_mosaic_mutable;p->mutable_count='+str(len(t['mutable']))+';\n p->layouts=demo_release_mosaic_layouts;p->layout_count='+str(len(entries))+';p->assets=&demo_release_mosaic_assets;p->assets_bytes='+str(m['bytes'])+';\n'
    for field,value in (('graph_policy_sha',t['graph_policy_sha256']),('runtime_deps_sha',t['runtime_dependencies_sha256']),('rodata_sha',t['rodata']['sha256'])):text+=' {const unsigned char v[32]='+array(value)+';memcpy(p->'+field+',v,32);}\n'
    return text+'}\n#endif\n'
def kernel_header(t,binding,flat,rodata):
    from scripts.mosaic.binding import ORDER,make_binding
    if len(binding)!=512 or struct.unpack_from('<8I',binding)!=(0x31424d56,1,512,6,12,1,1,0) or any(binding[480:]):raise ValueError('kernel binding header')
    hashes={name:binding[32+i*32:64+i*32].hex() for i,name in enumerate(ORDER)}
    if make_binding(hashes)!=binding or hashes['software_source']!=t['source_sha256'] or any(hashes[k]!=v for k,v in t['digests'].items()):raise ValueError('kernel trusted template binding')
    if len(flat)!=t['flat']['bytes'] or hashlib.sha256(flat).hexdigest()!=t['flat']['sha256'] or len(rodata)!=t['rodata']['bytes'] or hashlib.sha256(rodata).hexdigest()!=t['rodata']['sha256']:raise ValueError('actual compiled payload differs')
    text=template_header(t)+'\n'
    for name,raw in (('flat',flat),('rodata',rodata)):
        text+='static const vxl_u8 vxl_mosaic_'+name+'['+str(len(raw))+'] __attribute__((aligned(64)))={\n'
        text+='\n'.join(','.join(str(x) for x in raw[i:i+32])+',' for i in range(0,len(raw),32))+'\n};\n'
    p=t['runtime'];entries=[('.'+name+'='+str(p[name])+('ULL' if name in ('stack_base','stack_bytes') else 'u')) for name in ('runtime_bytes','entry','crt_base','crt_bytes','tls_base','tbss_bytes','tbss_stride','tbss_align','stack_stride','static_stack_bytes','stack_base','stack_bytes','rodata_gpu','rodata_bytes')]
    entries+=['.flat=vxl_mosaic_flat','.flat_bytes='+str(len(flat)),'.rodata=vxl_mosaic_rodata','.mutable_ranges=demo_release_mosaic_mutable','.mutable_count='+str(len(t['mutable'])),'.layouts=demo_release_mosaic_layouts','.layout_count='+str(len(t['layouts'])),'.assets=&demo_release_mosaic_assets','.assets_bytes='+str(t['assets']['bytes'])]
    for name,value in (('graph_policy_sha',t['graph_policy_sha256']),('runtime_deps_sha',t['runtime_dependencies_sha256']),('binding_sha',hashlib.sha256(binding).hexdigest()),('rodata_sha',t['rodata']['sha256'])):entries.append('.'+name+'='+c_array(digest(value)))
    entries.append('.binding={'+','.join(str(x) for x in struct.unpack_from('<8I',binding))+',{'+','.join(c_array(binding[32+i*32:64+i*32]) for i in range(14))+'},{0}}')
    return text+'static const struct mosaic_program vxl_mosaic_frozen_program={\n '+',\n '.join(entries)+'\n};\n'
