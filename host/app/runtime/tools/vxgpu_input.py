#!/usr/bin/env python3
"""Prepare and optionally register an immutable image input over Ethernet SSH.

This tool performs no inference, GPU submission, daemon startup or media write.
"""
import argparse, base64, hashlib, io, json, os, pathlib, re, shlex, stat
import subprocess, sys, time, uuid, platform

POLICIES={
    'hunet':'HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1',
    'mosaic':'MOSAIC_RGB_STRETCH_BILINEAR_CHW_F32_0_255_V1',
}
MAX_SOURCE=16<<20
MAX_PIXELS=16*1024*1024
def clock_ns():return time.monotonic_ns() if hasattr(time,'monotonic_ns') else int(time.monotonic()*1000000000)

def sha(raw):return hashlib.sha256(raw).hexdigest()

def prepare(source,kind,out,input_id,width=1280,height=960):
    """Return a receipt after executing the declared fixed-shape policy."""
    from PIL import Image
    import PIL, numpy as np
    started=clock_ns();source=pathlib.Path(source);out=pathlib.Path(out)
    if kind not in POLICIES or not re.fullmatch(r'[A-Za-z0-9_-]{1,96}',input_id):
        raise ValueError('supported model and safe input identifier required')
    if out.exists():raise FileExistsError(str(out))
    before=source.lstat()
    if not stat.S_ISREG(before.st_mode) or not 0<before.st_size<=MAX_SOURCE:
        raise ValueError('source must be a regular PNG/JPEG/BMP of at most16MiB')
    flags=os.O_RDONLY|getattr(os,'O_BINARY',0)|getattr(os,'O_NOFOLLOW',0)
    fd=os.open(str(source),flags)
    try:
        held=os.fstat(fd)
        with os.fdopen(fd,'rb',closefd=False) as stream:raw=stream.read(MAX_SOURCE+1)
        after=os.fstat(fd)
    finally:os.close(fd)
    identity=lambda s:(s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns)
    if identity(before)!=identity(held) or identity(held)!=identity(after) or len(raw)!=held.st_size:
        raise ValueError('source changed during bounded snapshot')
    with Image.open(io.BytesIO(raw)) as opened:
        if opened.format not in ('PNG','JPEG','BMP') or opened.width*opened.height>MAX_PIXELS:
            raise ValueError('unsupported format or decoded pixel limit exceeded')
        source_format=opened.format;original_size=list(opened.size);image=opened.convert('RGB')
    versions={'Pillow':PIL.__version__,'numpy':np.__version__}
    if kind=='hunet':
        import cv2
        versions['opencv']=cv2.__version__
        gray=cv2.cvtColor(np.asarray(image),cv2.COLOR_RGB2GRAY)
        _,binary=cv2.threshold(gray,0,255,cv2.THRESH_BINARY|cv2.THRESH_OTSU)
        h,w=binary.shape;scale=min(128.0/w,128.0/h)
        nw=max(1,int(w*scale));nh=max(1,int(h*scale))
        resized=cv2.resize(binary,(nw,nh),interpolation=cv2.INTER_AREA)
        padded=np.full((128,128),255,dtype=np.uint8)
        left=(128-nw)//2;top=(128-nh)//2;padded[top:top+nh,left:left+nw]=resized
        tensor=np.repeat(padded[None,:,:],3,axis=0).astype(np.float32)
        tensor=(tensor/np.float32(255)-np.float32(.5))/np.float32(.5)
        width=height=128
    else:
        if type(width) is not int or type(height) is not int or not 1<=width<=4096 or not 1<=height<=4096 or width*height*12>(64<<20):
            raise ValueError('bounded model-declared target shape required')
        if image.size!=(width,height):
            method=getattr(Image,'Resampling',Image).BILINEAR
            image=image.resize((width,height),method)
        tensor=np.asarray(image,dtype=np.float32).transpose(2,0,1)
    data=np.ascontiguousarray(tensor,dtype='<f4').tobytes()
    policy=POLICIES[kind]
    parameters=dict(source_sha=sha(raw),tensor_sha=sha(data),preprocessing_id=sha(policy.encode()),
        dtype='FP32',layout='CHW_F32',shape=json.dumps([3,height,width],separators=(',',':')),
        source_format=source_format,preprocessing=policy)
    files={input_id+'.source':raw,input_id+'.f32':data}
    record=dict(format='vxgpu-input-v1',input_id=input_id,entry_id=kind,parameters=parameters,
        source_bytes=len(raw),tensor_bytes=len(data),original_size=original_size,host=platform.node(),python=sys.version,
        orientation='stored-pixel-order',dependencies=versions,
        converter_sha256=sha(pathlib.Path(__file__).read_bytes()),
        preprocessing_begin_ns=started,preprocessing_end_ns=clock_ns(),
        gpu_raw_cycles=None,scope='EXECUTED_HOST_IMAGE_PREPROCESSING_NOT_INFERENCE',
        files=[dict(path=name,bytes=len(value),sha256=sha(value)) for name,value in files.items()])
    out.mkdir(parents=True,mode=0o700,exist_ok=False)
    for name,value in files.items():
        with (out/name).open('xb') as stream:stream.write(value)
        (out/name).chmod(0o400)
    with (out/'manifest.json').open('x',encoding='utf8') as stream:json.dump(record,stream,ensure_ascii=False,indent=2);stream.write('\n')
    (out/'manifest.json').chmod(0o400)
    return record

INSTALLER=r'''
import base64,hashlib,json,os,pathlib,platform,re,stat,sys
assert platform.machine()=='aarch64','actual board architecture required'
raw=sys.stdin.buffer.read((128<<20)+1);assert len(raw)<=128<<20,'upload bound'
q=json.loads(raw.decode('utf8'));name=q['record']['input_id'];assert re.fullmatch('[A-Za-z0-9_-]{1,96}',name)
boot=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip();assert boot==q['boot_id'],'boot changed during transfer'
directory='/run/vxgpud/input-inbox';fd=os.open(directory,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
ds=os.fstat(fd);assert ds.st_uid==os.geteuid() and stat.S_IMODE(ds.st_mode)==0o700,'owned0700 inbox'
saved=[]
for suffix,limit in (('.source',16<<20),('.f32',64<<20)):
 data=base64.b64decode(q['files'][suffix],validate=True);assert 0<len(data)<=limit
 digest=hashlib.sha256(data).hexdigest();expected=q['record']['parameters']['source_sha' if suffix=='.source' else 'tensor_sha'];assert digest==expected
 leaf=name+suffix
 try:target=os.open(leaf,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=fd)
 except FileExistsError:
  target=os.open(leaf,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=fd);s=os.fstat(target);assert stat.S_ISREG(s.st_mode) and s.st_uid==os.geteuid() and stat.S_IMODE(s.st_mode)==0o400 and s.st_nlink==1 and s.st_size==len(data),'existing input is not sealed identical data'
  with os.fdopen(target,'rb') as f:assert hashlib.sha256(f.read(limit+1)).hexdigest()==digest,'immutable id collision'
 else:
  with os.fdopen(target,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno());os.fchmod(f.fileno(),0o400)
 saved.append(dict(path=leaf,bytes=len(data),sha256=digest))
os.fsync(fd);os.close(fd);print(json.dumps(dict(boot_id=boot,files=saved,gpu_raw_cycles=None)))
'''

def upload(directory,board,ssh_config,entry_id=None,timeout=120):
    """Install sealed files and register once; never submit an inference."""
    directory=pathlib.Path(directory);record=json.loads((directory/'manifest.json').read_text(encoding='utf8'))
    sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
    from scripts.unified.joint_loop import Board
    transfer_out=directory/('transfer-'+uuid.uuid4().hex);transfer_out.mkdir(mode=0o700)
    transport=Board(board,ssh_config,'/run/vxgpud/control.sock',transfer_out)
    def remote(argv,stdin=None):
        code,raw=transport.command(argv,stdin,timeout=timeout)
        if code:raise RuntimeError(raw.decode('utf8','replace')[-2000:])
        return json.loads(raw.decode('utf8'))
    discovery=remote(['/usr/bin/vxgpuctl','--socket','/run/vxgpud/control.sock','models'])
    if not discovery.get('ok'):raise ValueError('service discovery rejected')
    boot=discovery['boot_id'];files={}
    for suffix,key in (('.source','source_sha'),('.f32','tensor_sha')):
        raw=(directory/(record['input_id']+suffix)).read_bytes()
        if sha(raw)!=record['parameters'][key]:raise ValueError('prepared input changed')
        files[suffix]=base64.b64encode(raw).decode()
    started=clock_ns()
    transfer=remote(['python3','-c',INSTALLER],json.dumps(dict(boot_id=boot,record=record,files=files),separators=(',',':')).encode())
    args=['/usr/bin/vxgpuctl','--socket','/run/vxgpud/control.sock','--timeout-ms','30000','input-register',
          '--boot-id',boot,'--client-id','image-upload','--request-id','upload-'+uuid.uuid4().hex,
          '--entry-id',entry_id or record['entry_id'],'--input-id',record['input_id'],
          '--parameters',json.dumps(record['parameters'],separators=(',',':'))]
    registered=remote(args)
    if not registered.get('ok'):raise ValueError('input registration rejected: '+json.dumps(registered))
    receipt=dict(format='vxgpu-input-transfer-v1',boot_id=boot,transfer=transfer,registered=registered,
        begin_ns=started,end_ns=clock_ns(),gpu_raw_cycles=None,submitted_inference=False)
    with (transfer_out/'receipt.json').open('x',encoding='utf8') as f:json.dump(receipt,f,indent=2);f.write('\n')
    return receipt

def existing_to_3d(board_name,ssh_config,out,selected=False,request_id=None):
    """Choose an actual retained Mosaic result and use the original file bridge."""
    sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
    from scripts.unified.joint_loop import Board,run
    out=pathlib.Path(out);out.mkdir(parents=True,mode=0o700,exist_ok=False)
    board=Board(board_name,ssh_config,'/run/vxgpud/control.sock',out)
    code,raw=board.command(['/usr/bin/vxgpuctl','--socket','/run/vxgpud/control.sock','models'])
    discovery=json.loads(raw)
    if code or not discovery.get('ok'):raise ValueError('actual service discovery required')
    boot=discovery['boot_id']
    if selected:
        reader="import os,stat,json; fd=os.open('/run/vxgpud/menu/texture-selection.json',os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK); s=os.fstat(fd); assert stat.S_ISREG(s.st_mode) and s.st_uid==os.geteuid() and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1 and 0<s.st_size<=4096; b=os.read(fd,4097); os.close(fd); print(b.decode('utf8'))"
        code,raw=board.command(['python3','-c',reader]);choice=json.loads(raw)
        if code or choice.get('boot_id')!=boot:raise ValueError('serial selection belongs to another boot')
    else:
        entries=[x for x in discovery['result']['entries'] if 'mosaic' in x['entry_id'].lower()]
        if not entries:raise ValueError('no installed Mosaic model')
        rows=board.rpc('results',dict(boot_id=boot),entry_id=entries[0]['entry_id'])['results']
        if request_id:rows=[x for x in rows if x['request_id']==request_id]
        if not rows:raise ValueError('no retained completed Mosaic result; no inference was submitted')
        if len(rows)==1:choice=rows[0]
        else:
            for index,row in enumerate(rows,1):print('%d  %s  %s'%(index,row['input_id'],row['request_id']))
            value=input('选择已有结果：').strip()
            if not value.isdigit() or not 1<=int(value)<=len(rows):raise ValueError('invalid result selection')
            choice=rows[int(value)-1]
    producer={key:choice[key] for key in ('boot_id','client_id','request_id')}
    return run(board,out,producer,300,reuse_existing=True,input_id=choice.get('input_id','EXISTING_RESULT'))

def showcase(args):
    """Execute HUNet once, Mosaic once, then the original texture/DP bridge."""
    sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
    from scripts.unified.joint_loop import Board,run,poll,mode
    start=time.monotonic();out=pathlib.Path(args.out);out.mkdir(parents=True,mode=0o700,exist_ok=False)
    record=dict(format='s8-three-app-showcase-v1',target_seconds=300,
        source_profile='NEW_GPU_REQUESTS_EXISTING_OR_UPLOADED_INPUTS',residency_profile='REQUIRES_OBSERVED_COLD_WARM_CLASSIFICATION',
        gpu_raw_cycles=None,sd='NOT_TOUCHED',pl='NOT_RECONFIGURED')
    board=Board(args.board,args.ssh_config,'/run/vxgpud/control.sock',out)
    try:
        _,raw=board.command(['/usr/bin/vxgpuctl','--socket','/run/vxgpud/control.sock','models']);discovery=json.loads(raw)
        if not discovery.get('ok'):raise ValueError('service discovery rejected')
        boot=discovery['boot_id'];inputs={'hunet':args.hunet_input,'mosaic':args.mosaic_input}
        for kind in ('hunet','mosaic'):
            image=getattr(args,kind+'_image')
            if image:
                prepared=out/(kind+'-input');meta=prepare(image,kind,prepared,'img-'+uuid.uuid4().hex)
                upload(prepared,args.board,args.ssh_config,kind);inputs[kind]=meta['input_id']
        record['inputs']=inputs;record['boot_id']=boot
        if not all(inputs.values()):raise ValueError('both input IDs or image files are required')
        producer=dict(boot_id=boot,client_id='b-showcase',request_id='hunet-'+uuid.uuid4().hex)
        remaining=lambda:max(1,int(300-(time.monotonic()-start)))
        record['mode_nn']=mode(board,boot,'nn',remaining())
        record['hunet_request']=producer
        # Exactly one submission per network. Status observations cannot replay it.
        record['hunet_submit']=board.rpc('submit',producer,entry_id='hunet',input_id=inputs['hunet'],parameters='{}')
        def state():
            value=board.rpc('status',producer)
            if value.get('status') in ('FAILED','CANCELLED'):raise ValueError('HUNet stopped: '+str(value))
            return value
        record['hunet_status']=poll(state,lambda value:value.get('status')=='COMPLETED',remaining(),'HUNet request')
        record['hunet_result']=board.rpc('result',producer)
        record['hunet_display']=board.rpc('display',producer,artifact=0)
        if time.monotonic()-start>=300:raise TimeoutError('300 second target exceeded before Mosaic; original HUNet retained')
        mosaic=dict(boot_id=boot,client_id='b-showcase',request_id='mosaic-'+uuid.uuid4().hex)
        record['mosaic_request']=mosaic;bridge=out/'mosaic-to-p8';bridge.mkdir(mode=0o700)
        record['mosaic_to_p8']=run(board,bridge,mosaic,remaining(),input_id=inputs['mosaic'])
        record['natural_exit']=0
    except BaseException as error:
        record.update(natural_exit=1,error=repr(error),status='INCOMPLETE_RETAIN_ORIGINAL_REQUESTS_NO_RESUBMIT');raise
    finally:
        record['wall_seconds']=time.monotonic()-start
        record['within_300_seconds_observed']=record.get('natural_exit')==0 and record['wall_seconds']<=300
        record['performance_acceptance']='REQUIRES_MATCHED_PHYSICAL_EVIDENCE_AND_COLD_WARM_REVIEW'
        with (out/'showcase-receipt.json').open('x',encoding='utf8') as f:json.dump(record,f,ensure_ascii=False,indent=2);f.write('\n')
    return record

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command');sub.required=True
    pre=sub.add_parser('prepare');pre.add_argument('source',type=pathlib.Path);pre.add_argument('--model',choices=tuple(POLICIES),required=True);pre.add_argument('--out',type=pathlib.Path,required=True);pre.add_argument('--input-id',default=None);pre.add_argument('--width',type=int,default=1280);pre.add_argument('--height',type=int,default=960)
    send=sub.add_parser('upload');send.add_argument('directory',type=pathlib.Path);send.add_argument('--board',required=True);send.add_argument('--ssh-config',required=True);send.add_argument('--entry-id')
    texture=sub.add_parser('texture');texture.add_argument('source',nargs='?',type=pathlib.Path);texture.add_argument('--board');texture.add_argument('--port',type=int,default=5007);texture.add_argument('--gui',action='store_true')
    reuse=sub.add_parser('result-to-3d');reuse.add_argument('--board',required=True);reuse.add_argument('--ssh-config',required=True);reuse.add_argument('--out',required=True,type=pathlib.Path);reuse.add_argument('--selected',action='store_true');reuse.add_argument('--request-id')
    show=sub.add_parser('showcase');show.add_argument('--board',required=True);show.add_argument('--ssh-config',required=True);show.add_argument('--out',required=True,type=pathlib.Path)
    for kind in ('hunet','mosaic'):
        show.add_argument('--'+kind+'-image',type=pathlib.Path);show.add_argument('--'+kind+'-input')
    args=p.parse_args(argv)
    try:
        if args.command=='prepare':result=prepare(args.source,args.model,args.out,args.input_id or 'img-'+uuid.uuid4().hex,args.width,args.height)
        elif args.command=='upload':result=upload(args.directory,args.board,args.ssh_config,args.entry_id)
        elif args.command=='result-to-3d':result=existing_to_3d(args.board,args.ssh_config,args.out,args.selected,args.request_id)
        elif args.command=='showcase':result=showcase(args)
        else:
            sender=pathlib.Path(__file__).resolve().parents[1]/'vendor/p8/desktop/texture_sender.py'
            if args.source and not args.gui and not args.board:raise ValueError('image transfer requires explicit --board; GUI lets you select the actual board')
            command=[sys.executable,str(sender),'--port',str(args.port)]
            if args.board:command+=['--board',args.board]
            if args.source:command.append(str(args.source))
            if args.gui or not args.source:command.append('--gui')
            return subprocess.call(command)
        print(json.dumps(result,ensure_ascii=False,indent=2));return 0
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError,ImportError) as error:
        print('vxgpu_input: '+str(error)+'; no automatic inference or retry',file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
