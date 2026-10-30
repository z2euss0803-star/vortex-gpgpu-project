"""Generated test catalog only; no raw descriptor from an untrusted client."""
import argparse,json,pathlib
from scripts.mosaic.contract import build_graph,encode_job
from scripts.mosaic.layout import plan_layout,check_layout
from tests.mosaic_s8.test_contract import fields
def write(out):
    root=pathlib.Path(__file__).resolve().parents[2]
    graph=build_graph(json.loads((root/'config/mosaic/model-v1.json').read_text()))
    layout=plan_layout(graph);check_layout(layout)
    out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False)
    (out/'graph-v1.json').write_text(json.dumps(graph,indent=2)+'\n')
    (out/'layout-v1.json').write_text(json.dumps(layout,indent=2)+'\n')
    raw=encode_job(**fields())
    lines=['static const unsigned char fixture_job[128] = {'+','.join(str(b) for b in raw)+'};',
           'static const struct mosaic_layout fixture_layout = {1280,960,{']
    for r in layout['regions']:lines.append('{%suLL,%suLL,%su,%su},'%(r['address'],r['bytes'],r['guard'],int(r['readonly'])))
    lines.append('},%suLL};'%layout['end'])
    small=plan_layout(build_graph(json.loads((root/'config/mosaic/model-v1.json').read_text()),12,8))
    lines.append('static const struct mosaic_layout fixture_small_layout = {12,8,{')
    for r in small['regions']:lines.append('{%suLL,%suLL,%su,%su},'%(r['address'],r['bytes'],r['guard'],int(r['readonly'])))
    lines.append('},%suLL};'%small['end'])
    for name,value in [('fixture_full_phase_spec',graph),('fixture_small_phase_spec',build_graph(json.loads((root/'config/mosaic/model-v1.json').read_text()),12,8))]:
        lines.append('static const unsigned '+name+'[77][3] = {')
        for layer in value['layers']:
            for op,count in zip(layer['ops'],layer['work_items']):lines.append('{%s,%s,%s},'%(layer['index'],op,count))
        lines.append('};')
    (out/'mosaic_contract_fixture.h').write_text('\n'.join(lines)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args();write(a.out)
