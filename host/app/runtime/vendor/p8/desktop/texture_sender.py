"""Texture preprocessing and finite-window reliable UDP sender (GUI and CLI)."""
import argparse
from datetime import datetime, timezone
import hashlib
from io import BytesIO
import ipaddress
import json
from pathlib import Path
import secrets
import socket
import struct
import threading
import time
from types import SimpleNamespace
import zlib
from PIL import Image, ImageOps, ImageStat

MAGIC=0x56545831
HELLO,BEGIN,DATA,STATUS,CANCEL,ACK,REPLY=range(1,8)
IDLE,RECEIVING,READY,APPLIED,FAILED=range(5)
HEADER=struct.Struct('!IBBH14I')
CHUNK=1200
WINDOW=24
RETRY_SECONDS=.25
MAX_ATTEMPTS=12

class TransferCancelled(Exception):pass
class TransferError(Exception):pass

def _background_rgb(source):
    """Deterministic mean of visible RGB pixels; hidden RGB never sets the matte."""
    visible=source.getchannel('A').point(lambda alpha:255 if alpha else 0)
    if not visible.getbbox():return (128,128,128)
    return tuple(int(channel+.5) for channel in ImageStat.Stat(source.convert('RGB'),mask=visible).mean)

def prepare_lod(source,size):
    """EXIF orientation, visible-color matte, proportional center cover; opaque RGBA8."""
    if size not in (512,2048):raise ValueError('texture size must be 512 or 2048')
    source=ImageOps.exif_transpose(source).convert('RGBA')
    matte=Image.new('RGBA',source.size,_background_rgb(source)+(255,))
    matte.alpha_composite(source)
    return ImageOps.fit(matte,(size,size),method=Image.Resampling.LANCZOS,centering=(.5,.5))

def prepare_image(path):
    source_path=Path(path).resolve();raw_source=source_path.read_bytes()
    with Image.open(BytesIO(raw_source)) as original:
        source_size=original.size
        orientation=original.getexif().get(274,1)
        source=ImageOps.exif_transpose(original).convert('RGBA')
    images=tuple(prepare_lod(source,size) for size in (512,2048))
    return SimpleNamespace(source_path=str(source_path),source_sha256=hashlib.sha256(raw_source).hexdigest(),source_orientation=orientation,source_size=source_size,oriented_size=source.size,
                           preprocessing=dict(policy='cover-visible-mean-v1',geometry='cover',crop='center',resampling='LANCZOS',
                                              transparent_background='visible_rgb_mean',background_rgb=_background_rgb(source),fully_transparent_fallback=(128,128,128)),
                           lods=tuple(img.tobytes() for img in images),preview=images[0])

def save_receipt(prepared,r,out=None):
    """Durable peer-reported APPLIED binding. Never claims visual/physical-board proof."""
    if r.get('state')!=APPLIED or not r.get('session') or not r.get('tx_id'):raise ValueError('only a complete APPLIED receipt can be saved')
    crcs=tuple(zlib.crc32(lod) for lod in prepared.lods)
    if crcs!=(r.get('small_crc'),r.get('large_crc')):raise ValueError('receipt texture CRC mismatch')
    timestamp=datetime.now(timezone.utc)
    if out is None:
        directory=Path(__file__).resolve().parents[3]/'reports/v23-warrior-adaptiveglaze-20261012/pc-receipts'
        out=directory/(timestamp.strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(r['tx_id'])+'.json')
    out=Path(out).resolve();out.parent.mkdir(parents=True,exist_ok=True)
    receipt=dict(schema='vortex-live-texture-pc-receipt-v1',evidence_scope='peer_reported_udp_applied_not_visual',
                 visual_acceptance=False,physical_board_proof=False,board_run=False,
                 source=dict(path=prepared.source_path,sha256=prepared.source_sha256,source_size=prepared.source_size,
                             exif_orientation=prepared.source_orientation,oriented_size=prepared.oriented_size),
                 preprocessing=dict(prepared.preprocessing),
                 lods=[dict(width=size,height=size,format='RGBA8',bytes=len(lod),crc32=zlib.crc32(lod),sha256=hashlib.sha256(lod).hexdigest()) for size,lod in zip((512,2048),prepared.lods)],
                 raw_applied=dict(r),session=r['session'],tx_id=r['tx_id'],peer_ip=r['peer_ip'],peer_port=r['peer_port'],
                 received_at_utc=r['received_at_utc'],saved_at_utc=timestamp.isoformat())
    temporary=out.with_suffix(out.suffix+'.tmp')
    temporary.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n',encoding='utf8');temporary.replace(out)
    return out

def encode(op,session=0,tx=0,lod=0,index=0,data=b'',small_crc=0,large_crc=0):
    return HEADER.pack(MAGIC,1,op,0,session,tx,lod,index,len(data),zlib.crc32(data),small_crc,large_crc,0,0,0,0,0,0)+data

def decode(data):
    if len(data)!=64:return None
    f=HEADER.unpack(data)
    if f[:2]!=(MAGIC,1) or f[2] not in (ACK,REPLY) or f[3]!=0 or f[12] not in range(5):return None
    return dict(op=f[2],session=f[4],tx_id=f[5],lod=f[6],index=f[7],reason=f[8],wire_hex=data.hex(),received_at_utc=datetime.now(timezone.utc).isoformat(),
                small_crc=f[10],large_crc=f[11],state=f[12],rgba_crc=f[13],depth_crc=f[14],asset_id=f[15],pose=f[16],gpu_jobs=f[17])

def send_texture(prepared,board_ip,port=5007,cancel=None,progress=None,timeout=120):
    """Return only an APPLIED receipt. READY means complete staging, never success."""
    ip=ipaddress.IPv4Address(board_ip)
    if ip.is_multicast or ip.is_unspecified or int(ip)==0xffffffff:raise ValueError('use one unicast IPv4 address')
    if not 1<=int(port)<=65535 or not 0<timeout<=180:raise ValueError('invalid port/timeout')
    if tuple(map(len,prepared.lods))!=(1048576,16777216):raise ValueError('both fixed RGBA8 LODs required')
    cancel=cancel or threading.Event();progress=progress or (lambda *_:None)
    deadline=time.monotonic()+timeout
    def check():
        if cancel.is_set():raise TransferCancelled('传输取消；板端如已取得纹理进入安全提交，不能撤销该事务')
        if time.monotonic()>deadline:raise TransferError('传输或等待 APPLIED 超时；不能声称更新成功')
    check()
    session=tx=0;success=False
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.connect((str(ip),int(port)));sock.settimeout(.025)
        def receive():
            try:return decode(sock.recv(1280))
            except socket.timeout:return None
        def exchange(request,accepted):
            for _ in range(MAX_ATTEMPTS):
                check();sock.send(request);until=time.monotonic()+RETRY_SECONDS
                while time.monotonic()<until:
                    check();r=receive()
                    if r and accepted(r):return r
            raise TransferError('握手/事务回复重试次数耗尽')
        try:
            hello=exchange(encode(HELLO),lambda r:r['op']==REPLY and r['session']!=0)
            session=hello['session']
            if hello['state'] in (RECEIVING,READY):raise TransferError('板端已有接收/提交事务，请稍后重试')
            if hello['tx_id']==0xffffffff:raise TransferError('事务计数耗尽，请重新初始化板端会话')
            tx=max(hello['tx_id']+1,secrets.randbelow(0x7ffffffe)+1)
            crcs=tuple(zlib.crc32(lod) for lod in prepared.lods)
            r=exchange(encode(BEGIN,session,tx,small_crc=crcs[0],large_crc=crcs[1]),lambda r:r['session']==session and r['tx_id']==tx)
            if r['state']!=RECEIVING:raise TransferError('板端未接受事务')
            total=sum(len(x) for x in prepared.lods);done=0
            chunks=[(lod,index) for lod,b in enumerate(prepared.lods) for index in range((len(b)+CHUNK-1)//CHUNK)]
            next_chunk=0;pending={};last_status=0;ready=False
            def send_block(key,attempt):
                lod,index=key;b=prepared.lods[lod][index*CHUNK:(index+1)*CHUNK]
                sock.send(encode(DATA,session,tx,lod,index,b));pending[key]=(time.monotonic(),attempt,len(b))
            while not ready:
                check()
                while len(pending)<WINDOW and next_chunk<len(chunks):
                    send_block(chunks[next_chunk],1);next_chunk+=1
                r=receive();now=time.monotonic()
                if r and r['session']==session and r['tx_id']==tx:
                    if r['state']==FAILED:raise TransferError('板端事务失败 reason='+str(r['reason']))
                    if r['state'] in (READY,APPLIED):ready=True
                    elif r['op']==ACK:
                        key=(r['lod'],r['index'])
                        if key in pending:done+=pending.pop(key)[2];progress(done,total,'接收中')
                if ready:break
                # Status recovers a lost final DATA ACK after staging enters READY.
                if now-last_status>=RETRY_SECONDS:sock.send(encode(STATUS,session,tx));last_status=now
                for key,(sent,attempt,size) in list(pending.items()):
                    if now-sent>=RETRY_SECONDS:
                        if attempt>=MAX_ATTEMPTS:raise TransferError('分块重试次数耗尽')
                        send_block(key,attempt+1)
            progress(total,total,'纹理 READY，等待安全提交与当前视角重绘')
            while r['state']!=APPLIED:
                check();sock.send(encode(STATUS,session,tx));until=time.monotonic()+RETRY_SECONDS
                while time.monotonic()<until:
                    check();candidate=receive()
                    if candidate and candidate['session']==session and candidate['tx_id']==tx:
                        r=candidate
                        if r['state']==FAILED:raise TransferError('提交失败 reason='+str(r['reason']))
                        if r['state']==APPLIED:break
            if (r['small_crc'],r['large_crc'])!=crcs:raise TransferError('APPLIED纹理CRC与发送内容不同')
            r.update(peer_ip=str(ip),peer_port=int(port),evidence_scope='peer_reported_udp_applied_not_visual')
            success=True;progress(total,total,'APPLIED：板端自报安全提交、当前视角绘制与退休');return r
        finally:
            if session and tx and not success:
                try:
                    for _ in range(3):sock.send(encode(CANCEL,session,tx))
                except OSError:pass

def main():
    ap=argparse.ArgumentParser(description='PC纹理发送器（仅手动运行才发包）')
    ap.add_argument('image',nargs='?');ap.add_argument('--board',default='192.168.10.2');ap.add_argument('--port',type=int,default=5007);ap.add_argument('--gui',action='store_true')
    args=ap.parse_args()
    if args.gui or not args.image:
        from texture_gui import main as gui_main
        gui_main();return
    prepared=prepare_image(args.image)
    print('source',prepared.source_size,'oriented',prepared.oriented_size,'RGBA bytes',tuple(map(len,prepared.lods)))
    last=[-1]
    def progress(done,total,status):
        percent=done*100//total
        if percent!=last[0]:print(percent,status);last[0]=percent
    try:
        r=send_texture(prepared,args.board,args.port,progress=progress)
        print('APPLIED receipt',r);print('Receipt saved:',save_receipt(prepared,r))
    except (TransferError,TransferCancelled,OSError,ValueError) as e:ap.exit(1,str(e)+'\n')

if __name__=='__main__':main()
