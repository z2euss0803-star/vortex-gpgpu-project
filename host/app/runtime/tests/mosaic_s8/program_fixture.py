"""TEST_ONLY metadata, never a deployable program or image."""
import argparse,pathlib,struct,hashlib,json
from scripts.mosaic.binding import ORDER,make_binding
from scripts.mosaic.contract import build_graph
from scripts.mosaic.layout import plan_layout
def generate(manifest,out,width=12,height=8,prefix='program_'):
    root=pathlib.Path(__file__).resolve().parents[2];assets=json.loads(pathlib.Path(manifest).read_text())
    model=json.loads((root/'config/mosaic/model-v1.json').read_text())
    graph=build_graph(model,width,height);layout=plan_layout(graph)
    graph_sha=hashlib.sha256(('TEST_ONLY_GRAPH_%s_%s'%(width,height)).encode()).digest();layout_sha=bytes.fromhex(layout['sha256'])
    policy=hashlib.sha256(b'TEST_ONLY_FROZEN_GRAPH_POLICY').digest();deps=bytes([4])*32
    encoded=struct.pack('<II',width,height)
    for r in layout['regions']:encoded+=struct.pack('<QQII',r['address'],r['bytes'],r['guard'],int(r['readonly']))
    encoded+=struct.pack('<Q',layout['end']);assert len(encoded)==232
    layout_root=hashlib.sha256(struct.pack('<4I',0x314c4d56,1,1,296)+encoded+graph_sha+layout_sha).hexdigest()
    graph_root=hashlib.sha256(struct.pack('<4I',0x31474d56,1,1,32)+policy+struct.pack('<II',width,height)+graph_sha).hexdigest()
    runtime=struct.pack('<4I',0x31524d56,1,2048,2)+struct.pack('<8I',0x10002000,4096,0x80000200,4,16,16,8192,1024)+struct.pack('<QQ',0xfff00000,0x100000)+deps+struct.pack('<4I',128,64,256,1792)
    flat=bytes(range(256));digests={name:hashlib.sha256(('TEST_ONLY_'+name).encode()).hexdigest() for name in ORDER}
    digests.update(device_flat=hashlib.sha256(flat).hexdigest(),model=assets['source_sha256']['model'],assets=assets['sha256'],graph_catalog=graph_root,layout_catalog=layout_root,rv32_runtime=hashlib.sha256(runtime).hexdigest())
    binding=make_binding(digests);values=dict(program_binding=binding,program_binding_sha=hashlib.sha256(binding).digest(),program_flat=flat,program_policy=policy,program_deps=deps,program_graph=graph_sha,program_layout=layout_sha)
    out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False)
    lines=['/* TEST_ONLY, no valid GPU executable. */']
    for name,value in values.items():lines.append('static const unsigned char '+name.replace('program_',prefix,1)+'[%d] = {'%len(value)+','.join(str(x) for x in value)+'};')
    (out/'program_fixture.h').write_text('\n'.join(lines)+'\n')
    (out/'metadata.json').write_text(json.dumps(dict(scope='TEST_ONLY_PROGRAM_REGISTRY_METADATA_NOT_GPU',digests=digests,raw_gpu_cycles=None),indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--manifest',required=True);p.add_argument('--out',required=True);p.add_argument('--width',type=int,default=12);p.add_argument('--height',type=int,default=8);p.add_argument('--prefix',default='program_');a=p.parse_args();generate(a.manifest,a.out,a.width,a.height,a.prefix)
