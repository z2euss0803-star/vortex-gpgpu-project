"""Independent group simulation, never generated from driver's expected-owner code."""
from pathlib import Path
import sys,struct
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.hunet_opt.catalog import compile_catalog
c=compile_catalog(sys.argv[1]);stages={s['stage_id']:s for s in c['matrix_layout']['matrix_stages']}
def matrix(stage):
    s=stages[stage];groups=[]
    for d in s['descriptors']:
        cols=d[4]//4
        for row in range(d[3]//8):
            for col in range(0,cols,c['tile_n']):
                groups.append([d[10]+row*cols+k for k in range(col,min(cols,col+c['tile_n']))])
    active=min(4,(len(groups)+3)//4);assignment=[]
    for core in range(active):assignment.extend([core+1]*(len(groups)//active+(core<len(groups)%active)))
    owners=[None]*s['gemm_grid_tiles']
    for tiles,core in zip(groups,assignment):
        for tile in tiles:
            assert owners[tile] is None;owners[tile]=core
    assert None not in owners
    return owners
out=[]
for e in c['entries']:
    if e['kind']!='GPU' or not e['owner_tiles']:continue
    values=matrix(e['first_stage']) if e['op']==15 else [v for n in e['nodes'] if n[0]==15 for v in matrix(n[1])]
    assert len(values)==e['owner_tiles'];out.extend(values)
Path(sys.argv[2]).write_bytes(struct.pack('<%dI'%len(out),*out))
