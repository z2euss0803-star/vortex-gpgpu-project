"""Fixed remote commands; all untrusted metadata travels over stdin, never a shell."""
import hashlib,json,re,subprocess
from pathlib import Path
from .contracts import MAX_JSON,validate_asset

ACTIONS=('identity','select-app','stage','run-mosaic','run-hunet','status','display','export')


class TransportError(RuntimeError):
    def __init__(self,message,ambiguous=False):super().__init__(message);self.ambiguous=ambiguous

class DemoTransport:
    def __init__(self,config,process=None):
        if not isinstance(config,dict) or any(x not in config for x in ('ssh','scp','ssh_config','host','timeout')):raise ValueError('explicit connection config required')
        if not isinstance(config['host'],str) or re.fullmatch('[A-Za-z0-9][A-Za-z0-9_.-]{0,127}',config['host']) is None:raise ValueError('unsafe SSH alias')
        if type(config['timeout']) is not int or not 1<=config['timeout']<=60:raise ValueError('bounded timeout required')
        for key in ('ssh','scp','ssh_config'):
            if not isinstance(config[key],str) or not config[key] or '\0' in config[key]:raise ValueError('invalid tool/config path')
        self.config=dict(config);self.process=process or subprocess.run

    def _invoke(self,argv,data=None,ambiguous=False):
        try:result=self.process(argv,input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=self.config['timeout'],shell=False)
        except (OSError,subprocess.TimeoutExpired) as error:raise TransportError(str(error),ambiguous=ambiguous)
        if len(result.stdout)>MAX_JSON or len(result.stderr)>MAX_JSON:raise TransportError('oversize transport response',ambiguous=ambiguous)
        return result

    def call(self,command,request):
        if command not in ACTIONS or not isinstance(request,dict):raise ValueError('fixed action/object required')
        payload=json.dumps(request,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode('utf-8')
        if len(payload)>MAX_JSON:raise ValueError('request too large')
        argv=[self.config['ssh'],'-F',self.config['ssh_config'],'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',self.config['host'],
              '/usr/bin/vxl_demo '+command+' --request-stdin']
        uncertain=command.startswith('run-')
        result=self._invoke(argv,payload,uncertain)
        def unique(pairs):
            obj={}
            for key,value in pairs:
                if key in obj:raise ValueError('duplicate JSON key')
                obj[key]=value
            return obj
        def nonfinite(_):raise ValueError('nonfinite JSON')
        try:reply=json.loads(result.stdout.decode('utf-8'),object_pairs_hook=unique,parse_constant=nonfinite)
        except (ValueError,UnicodeError):raise TransportError('invalid/truncated JSON response',ambiguous=uncertain)
        if not isinstance(reply,dict) or type(reply.get('ok')) is not bool or not isinstance(reply.get('scope'),str):raise TransportError('invalid response contract',ambiguous=uncertain)
        if result.returncode or not reply['ok']:raise TransportError('remote action rejected: '+str(reply.get('state','UNKNOWN')),ambiguous=False)
        if reply['scope'] in ('MOCK','OFFLINE_ADAPTERS_NOT_CONNECTED') and reply.get('raw_cycles') is not None:raise TransportError('offline cycles forbidden')
        return reply

    def export_run(self,request,output):
        run=request.get('run_id')
        if not isinstance(run,str) or re.fullmatch('[0-9a-f]{32}',run) is None:raise ValueError('fixed run id required')
        reply=self.call('export',{'run_id':run});rows=reply.get('files');seen=set();total=0
        if reply.get('state')!='EXPORT_MANIFEST_READY' or reply.get('run_id')!=run or not isinstance(rows,list) or not 1<=len(rows)<=512:raise TransportError('bounded export manifest required')
        for row in rows:
            name=row.get('name');size=row.get('bytes');digest=row.get('sha256')
            if not isinstance(name,str) or re.fullmatch('[a-z0-9._-]{1,128}',name) is None or '..' in name or name in ('.','..','download-receipt.json','remote-export-manifest.json') or name.startswith('.download-') or name in seen or type(size)!=int or not 0<=size<=64*1024**2 or not isinstance(digest,str) or re.fullmatch('[0-9a-f]{64}',digest) is None:raise ValueError('unsafe export member')
            seen.add(name);total+=size
        if total>512*1024**2:raise ValueError('export exceeds explicit size limit')
        root=Path(output).absolute()
        if any(p.is_symlink() for p in [root]+list(root.parents)):raise ValueError('ordinary export destination required')
        root.mkdir(parents=True,exist_ok=False);receipt=dict(state='EXPORT_INCOMPLETE',run_id=run,files=rows,remote_deleted=False)
        (root/'remote-export-manifest.json').write_text(json.dumps(reply,sort_keys=True,indent=2)+'\n',encoding='utf8')
        try:
            for i,row in enumerate(rows):
                target=root/('.download-%03d'%i)
                remote=self.config['host']+':/var/lib/vxl-demo/'+run+'/'+row['name']
                argv=[self.config['scp'],'-O','-F',self.config['ssh_config'],'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',remote,str(target)]
                result=self._invoke(argv)
                if result.returncode or target.is_symlink() or not target.is_file() or target.stat().st_size!=row['bytes']:raise TransportError('export transfer incomplete')
                h=hashlib.sha256()
                with target.open('rb') as f:
                    for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
                if h.hexdigest()!=row['sha256']:raise TransportError('export content mismatch')
                target.rename(root/row['name'])
            receipt['state']='EXPORTED_AND_VERIFIED'
        except Exception as exc:
            receipt['error']=str(exc);raise
        finally:(root/'download-receipt.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n',encoding='utf8')
        return receipt

    @staticmethod
    def _check_local(path,asset):
        path=Path(path)
        if any(p.is_symlink() for p in [path]+list(path.parents)) or not path.is_file():raise ValueError('ordinary asset file required')
        if path.stat().st_size!=asset['bytes']:raise ValueError('truncated asset')
        h=hashlib.sha256();total=0
        with path.open('rb') as source:
            for block in iter(lambda:source.read(1024*1024),b''):
                total+=len(block)
                if total>asset['bytes']:raise ValueError('asset grew during read')
                h.update(block)
        if total!=asset['bytes'] or h.hexdigest()!=asset['sha256']:raise ValueError('asset identity mismatch')
        return str(path.resolve())

    def stage(self,local_path,asset):
        asset=validate_asset(asset);local=self._check_local(local_path,asset)
        remote=self.config['host']+':/var/lib/vxl-demo/incoming/'+asset['sha256']+'.part'
        argv=[self.config['scp'],'-O','-F',self.config['ssh_config'],'-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',local,remote]
        result=self._invoke(argv)
        if result.returncode:raise TransportError('asset transfer failed; not finalized')
        self._check_local(local_path,asset)
        reply=self.call('stage',asset)
        if reply.get('asset_sha256')!=asset['sha256']:raise TransportError('stage receipt identity mismatch')
        return reply
