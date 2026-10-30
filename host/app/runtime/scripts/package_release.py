"""Offline evidence package. No deployment commands or SD access."""
import argparse,hashlib,json,pathlib,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True);args=parser.parse_args()
    output=pathlib.Path(args.output).resolve()
    files=[p for p in ROOT.rglob('*') if p.is_file() and not any(x in p.relative_to(ROOT).parts for x in ['.git','.toolchain','__pycache__']) and p.resolve()!=output and p.suffix not in ['.o','.pyc'] and not p.name.startswith('source.tar')]
    manifest={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    with tarfile.open(str(output),'w:gz') as tar:
        for p in sorted(files):tar.add(str(p),arcname=str(p.relative_to(ROOT)).replace('\\','/'))
    output.with_suffix(output.suffix+'.manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'package':str(output),'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'files':len(files)},indent=2))
