"""Exact original output SHA on actual new recipe and frozen word interpreter.

This is host composition evidence, never GPU/RTL/timing/physical display.
"""
import hashlib,json,pathlib

def verify(elf,out,run):
    source=pathlib.Path('/home/z2euss/workspace/image_studio_linux_20261021/input')
    raw=(source/'MANIFEST_SHA256.json').read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='96dc7902f3b06d9047d1c6d511ec8ca6ee1a6c96021420dc75ebf825c2f415e3'
    manifest={r['path']:r for r in json.loads(raw)['files']}
    prefix='reports/v23-image-studio-720p-fast-20261016/'
    def frozen(name):
        p=source/name;r=manifest[name];b=p.read_bytes()
        assert len(b)==r['bytes'] and hashlib.sha256(b).hexdigest()==r['sha256']
        return p
    proofs=[]
    for mode,input_name,oracle_name in [('colors','pipeline-colors/input.rgba','pipeline-colors/oracle.json'),('replay','pipeline-colors/input.rgba','replay/oracle.json'),('full','reference/input.rgba','pipeline/oracle.json')]:
        dest=out/mode;dest.mkdir();inp=frozen(prefix+input_name);oracle_path=frozen(prefix+oracle_name)
        oracle=json.loads(oracle_path.read_bytes());requests=frozen(prefix+'replay/requests.txt')
        table=frozen('gpu_programs/tables-64KiB.bin')
        text=run([elf,mode,inp,requests,dest,table]);assert 'PASS_HOST' in text and 'gpu_raw_cycles=null' in text
        results=oracle['outputs' if mode=='replay' else 'results']
        for r in results:
            if mode=='replay':name='tx-%u.rgba'%r['tx']
            elif mode=='full':name='style-%u.rgba'%r['style']
            else:name='style-%u-p%u'%(r['style'],r['preset'])+('-b%u'%r['block'] if 'block' in r else '')+'.rgba'
            p=dest/name;b=p.read_bytes();actual=hashlib.sha256(b).hexdigest();assert actual==r['sha256'],(mode,name,actual,r['sha256'])
            proofs.append(dict(mode=mode,path=str(p),bytes=len(b),sha256=actual))
    assert len(proofs)==91
    return dict(scope='NEW_RECIPE_ORIGINAL_RV32_HOST_INTERPRETER_NOT_GPU',outputs=proofs,gpu_raw_cycles=None)
