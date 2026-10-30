"""Private bounded Mosaic graph and LE job wire; no target execution."""
import argparse,hashlib,json,pathlib,struct,zlib
KEYS=('run_id','binding_tag','generation','sequence','op','layer','tile_first','tile_count','core_mask','input_epoch','layout_tag','graph_tag','flags')
TAGS=('run_id','binding_tag','layout_tag','graph_tag')
U32=('generation','sequence','op','layer','tile_first','tile_count','core_mask','input_epoch','flags')
def tagged(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def geometry():
    rows=[(3,32,9,1,1,True,True,False,False),(32,64,3,2,1,True,True,False,False),
          (64,128,3,2,1,True,True,False,False)]
    for _ in range(5):
        rows.extend([(128,128,3,1,1,True,True,True,False),(128,128,3,1,1,True,False,False,True)])
    return rows+[(128,64,3,1,2,True,True,False,False),(64,32,3,1,2,True,True,False,False),(32,3,9,1,1,False,False,False,False)]
def build_graph(model, width=1280, height=960):
    if type(width)!=int or type(height)!=int or not 8<=width<=1280 or not 8<=height<=960 or width%4 or height%4:raise ValueError('SHAPE')
    if model.get('version')!=1 or len(model.get('layers',[]))!=16:raise ValueError('MODEL')
    fields=('ci','co','k','stride','upsample','norm','relu','save','residual')
    graph=dict(version=1,width=width,height=height,model_sha256=tagged(model),sources=model['sources'],layers=[])
    h,w=height,width;asset_cursor=6400
    for i,(m,g) in enumerate(zip(model['layers'],geometry())):
        if tuple(m[x] for x in fields)!=g:raise ValueError('FROZEN_GEOMETRY')
        ci,co,k,stride,up,norm,relu,save,residual=g
        oh,ow=h*up//stride,w*up//stride
        row=dict(index=i,name=m['name'],ci=ci,co=co,k=k,stride=stride,upsample=up,norm=norm,relu=relu,save=save,residual=residual,
                 input_shape=[ci,h,w],output_shape=[co,oh,ow],K=ci*k*k,Kpad=(ci*k*k+31)//32*32,
                 input_role='fp_a' if i%2==0 else 'fp_b',output_role='fp_b' if i%2==0 else 'fp_a')
        row['conv_tiles']=((co+7)//8)*((oh*ow+7)//8)
        row['ops']=[1,2]+([3,4] if norm else [])+([5] if relu else [])+([6] if residual else [])
        row['work_items']=[(ci*h*w+255)//256,row['conv_tiles']]+([co,(co*oh*ow+255)//256] if norm else [])+([(co*oh*ow+255)//256] if relu else [])+([(co*oh*ow+255)//256] if residual else [])
        row['weight_segments']=[]
        for name,size in [('q16',co*row['Kpad']*2),('coeff',co*8),('sA',4)]+([('norm_gamma',co*4),('norm_beta',co*4)] if norm else []):
            asset_cursor=(asset_cursor+63)//64*64
            row['weight_segments'].append(dict(kind=name,offset=asset_cursor,bytes=size));asset_cursor+=size
        # The first version submits one layer-phase range; tiles loop on device.
        # Ranges have a canonical contiguous order, never arbitrary graph edits.
        graph['layers'].append(row);h,w=oh,ow
    graph.update(asset_format='MS8AST1',asset_bytes=(asset_cursor+63)//64*64)
    graph['sha256']=tagged(graph)
    return graph

def encode_job(**fields):
    if set(fields)!=set(KEYS):raise ValueError('FIELDS')
    for name in TAGS:
        if not isinstance(fields[name],bytes) or len(fields[name])!=16:raise ValueError('TAG')
    for name in U32:
        if type(fields[name])!=int or not 0<=fields[name]<2**32:raise ValueError('UINT32')
    raw=bytearray(128);struct.pack_into('<IHH',raw,0,0x31434d56,1,128)
    raw[8:24]=fields['run_id'];raw[24:40]=fields['binding_tag']
    struct.pack_into('<8I',raw,40,*(fields[n] for n in U32[:8]))
    raw[72:88]=fields['layout_tag'];raw[88:104]=fields['graph_tag']
    struct.pack_into('<I',raw,108,fields['flags'])
    struct.pack_into('<I',raw,104,zlib.crc32(raw)&0xffffffff)
    return bytes(raw)

def decode_job(raw):
    if not isinstance(raw,(bytes,bytearray)) or len(raw)!=128:raise ValueError('WIRE_BYTES')
    if struct.unpack_from('<IHH',raw)!=(0x31434d56,1,128) or any(raw[112:]):raise ValueError('HEADER_RESERVED')
    crc=bytearray(raw);crc[104:108]=b'\0'*4
    if zlib.crc32(crc)&0xffffffff!=struct.unpack_from('<I',raw,104)[0]:raise ValueError('CRC')
    d=dict(zip(U32[:8],struct.unpack_from('<8I',raw,40)))
    d.update(run_id=bytes(raw[8:24]),binding_tag=bytes(raw[24:40]),layout_tag=bytes(raw[72:88]),graph_tag=bytes(raw[88:104]),flags=struct.unpack_from('<I',raw,108)[0])
    return d

def validate_job(raw, expected):
    d=decode_job(raw)
    if any(d[k]!=expected[k] for k in KEYS if k not in ('tile_count','flags')):raise ValueError('IDENTITY_OR_ORDER')
    if d['core_mask']!=15 or d['layer']>=16 or d['op'] not in range(1,7) or d['flags']&~1:raise ValueError('OP_FLAGS')
    if not d['generation'] or not d['input_epoch'] or not any(d['run_id']):raise ValueError('ZERO_IDENTITY')
    if not 0<d['tile_count']<=expected['total_tiles']-d['tile_first']:raise ValueError('TILE_RANGE')
    return True

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--model',default='config/mosaic/model-v1.json');p.add_argument('--out',required=True);a=p.parse_args()
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=False)
    graph=build_graph(json.loads(pathlib.Path(a.model).read_text()))
    (out/'graph-v1.json').write_text(json.dumps(graph,indent=2)+'\n')
