"""Download/verify the pinned R13 release. Never access a board or SD card."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, urllib.request, urllib.parse, zipfile

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()

class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        redirected=super().redirect_request(req,fp,code,msg,headers,newurl)
        if redirected and urllib.parse.urlsplit(newurl).netloc != urllib.parse.urlsplit(req.full_url).netloc:
            redirected.remove_header('Authorization')
        return redirected

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--dest',required=True)
    parser.add_argument('--archive',help='Verify a manually downloaded archive without network access')
    args=parser.parse_args()
    cfg=json.loads((Path(__file__).parent/'assets.json').read_text(encoding='utf8'))
    out=Path(args.dest);out.mkdir(parents=True,exist_ok=True)
    if any(out.iterdir()):raise SystemExit('Destination must be empty; preserve prior files.')
    spec=cfg['assets'][0];target=out/spec['name']
    if args.archive:
        with Path(args.archive).open('rb') as src,target.open('xb') as dst:shutil.copyfileobj(src,dst)
    else:
        headers={'User-Agent':'R13-verified-download','Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28'}
        token=os.environ.get('GITHUB_TOKEN')
        if token:headers['Authorization']='Bearer '+token
        api='https://api.github.com/repos/z2euss0803-star/vortex-gpgpu-project/releases/tags/'+cfg['tag']
        opener=urllib.request.build_opener(SafeRedirect())
        with opener.open(urllib.request.Request(api,headers=headers),timeout=120) as response:release=json.load(response)
        asset=next((a for a in release.get('assets',[]) if a['name']==spec['name']),None)
        if asset is None or asset['size']!=spec['bytes']:raise SystemExit('Expected release asset unavailable or different size.')
        request=urllib.request.Request(asset['url'],headers={**headers,'Accept':'application/octet-stream'})
        with opener.open(request,timeout=120) as src,target.open('xb') as dst:shutil.copyfileobj(src,dst,1048576)
    if digest(target)!=spec['sha256'] or target.stat().st_size!=spec['bytes']:
        raise SystemExit('Archive identity mismatch; retain failed file.')
    with zipfile.ZipFile(target) as archive:
        for info in archive.infolist():
            if Path(info.filename).name!=info.filename:raise SystemExit('Unexpected archive path')
        required={'BOOT.BIN','image.ub','boot.scr','kernel.config','SHA256SUMS'}
        if set(archive.namelist())!=required:raise SystemExit('Archive members differ from declared boot bundle.')
        if archive.testzip():raise SystemExit('ZIP CRC failure')
        archive.extractall(out)
    sums={}
    for line in (out/'SHA256SUMS').read_text(encoding='utf8').splitlines():
        expected,name=line.split(None,1);sums[name.strip()]=expected
    for name in ['BOOT.BIN','image.ub','boot.scr','kernel.config']:
        if name not in sums or digest(out/name)!=sums[name]:raise SystemExit('Boot file identity mismatch: '+name)
    print('Boot files verified; no device or media operations.')

if __name__=='__main__':main()
