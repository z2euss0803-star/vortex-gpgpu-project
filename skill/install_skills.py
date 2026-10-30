"""Copy standalone skills to an explicit client directory without overwriting."""
import argparse,json,shutil
from pathlib import Path

def install(source,destination,names=None):
    source=Path(source).resolve();destination=Path(destination).resolve()
    if destination==source or source in destination.parents:raise ValueError('destination cannot be inside the source bundle')
    available={p.name:p for p in source.iterdir() if p.is_dir() and not p.is_symlink() and (p/'SKILL.md').is_file()}
    selected=sorted(available) if names is None else list(dict.fromkeys(names))
    if not selected or any(n not in available for n in selected):raise ValueError('select existing skill names only')
    if any((destination/n).exists() or (destination/n).is_symlink() for n in selected):raise ValueError('an installed name already exists; no files overwritten')
    # Reject links in the source instead of silently copying outside the bundle.
    for n in selected:
        if any(p.is_symlink() for p in available[n].rglob('*')):raise ValueError('source skill contains a symlink')
    destination.mkdir(parents=True,exist_ok=True)
    for n in selected:shutil.copytree(available[n],destination/n,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    return selected

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--destination',required=True);p.add_argument('--skill',action='append');a=p.parse_args()
    try:r=install(Path(__file__).parent,a.destination,a.skill)
    except (OSError,ValueError) as e:print(json.dumps({'status':'REFUSED','reason':str(e)}));return 1
    print(json.dumps({'status':'COPIED','skills':r,'destination':str(Path(a.destination).resolve())}));return 0

if __name__=='__main__':raise SystemExit(main())
