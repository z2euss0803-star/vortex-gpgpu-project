"""Explicit CLI or GUI action only. APPLIED is peer-reported, not visual proof."""
from pathlib import Path
import sys
S=Path(__file__).resolve().parents[1];sys.path.insert(0,str(S))
import argparse,dataclasses,json,queue,re,socket,struct,threading,time,zlib
from contract import Params,Rect,validate
from prepare_image import prepare_image
HELLO,BEGIN,DATA,STATUS,CANCEL,STYLE,ACK,REPLY=range(1,9)
IDLE,RECEIVING,READY,PROCESSING,APPLIED,FAILED=range(6)
FIELDS='session tx input_id style preset block width height offset length payload_crc image_crc state detail'.split()
DEFAULT_BOARD='192.168.10.2'
@dataclasses.dataclass
class Header:
 op:int=HELLO;session:int=0;tx:int=0;input_id:int=0;style:int=0;preset:int=0;block:int=0;width:int=1280;height:int=720;offset:int=0;length:int=0;payload_crc:int=0;image_crc:int=0;state:int=0;detail:int=0
 def encode(self,payload=b''):
  self.length=len(payload);self.payload_crc=zlib.crc32(payload) if payload else 0
  return struct.pack('!IBBH14I',0x56534934,1,self.op,0,*(getattr(self,f) for f in FIELDS))+payload
 @classmethod
 def decode(cls,packet):
  if len(packet)<64:raise ValueError('short header')
  magic,version,op,reserved,*v=struct.unpack('!IBBH14I',packet[:64])
  if magic!=0x56534934 or version!=1 or reserved or op not in range(1,9):raise ValueError('bad header')
  h=cls(op=op,**dict(zip(FIELDS,v)))
  if len(packet)!=64+h.length or h.length>1200 or (zlib.crc32(packet[64:]) if h.length else 0)!=h.payload_crc:raise ValueError('payload length/CRC')
  return h
def parse_applied(h,body,q):
 if h.op!=REPLY or h.state!=APPLIED or h.length!=64 or len(body)!=64:raise ValueError('missing APPLIED metrics')
 for f in ('session','tx','input_id','style','preset','block','width','height','image_crc'):
  if getattr(h,f)!=getattr(q,f):raise ValueError('APPLIED identity '+f)
 v=struct.unpack('!16I',body);metrics=dict(cache_hit=v[0],pass_count=v[1],jobs_before=v[2],jobs_after=v[3])
 for i,key in enumerate(('gpu_cycles','generate_ticks','blit_ticks','retire_ticks','timer_hz','total_ticks')):metrics[key]=v[4+2*i]|(v[5+2*i]<<32)
 if metrics['cache_hit'] not in (0,1) or ((v[3]-v[2])&0xffffffff)!=v[1] or (v[0] and v[1]):raise ValueError('inconsistent jobs/cache')
 expected={0:(0,),1:(1,5),2:(6,),3:(1,7,11),4:(1,2)}[h.style]
 if not v[0] and v[1] not in expected:raise ValueError('incorrect pass count')
 if not metrics['timer_hz']:raise ValueError('missing timer frequency')
 return dict(scope='peer_reported_applied_not_visual',session=h.session,tx=h.tx,input_id=h.input_id,style=h.style,preset=h.preset,block=h.block,input_crc=h.image_crc,output_crc=h.detail,metrics=metrics)
class Client:
 def __init__(self,board=DEFAULT_BOARD,port=5011,timeout=180,stop=None,window=8):
  if window not in (1,4,8):raise ValueError('window must be 1, 4 or 8')
  self.window=window;self.peer=(socket.gethostbyname(board),port);self.timeout=timeout;self.stop=stop or threading.Event();self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sock.settimeout(.25);self.header=None
 def close(self):self.sock.close()
 def exchange(self,q,body=b'',accept=None):
  packet=q.encode(body);deadline=min(time.monotonic()+min(10,self.timeout),getattr(self,'deadline',float('inf')))
  while time.monotonic()<deadline:
   if self.stop.is_set():raise InterruptedError('cancelled')
   self.sock.sendto(packet,self.peer)
   until=time.monotonic()+.25
   while time.monotonic()<until:
    try:raw,peer=self.sock.recvfrom(1280)
    except socket.timeout:break
    if peer!=self.peer:continue
    try:r=Header.decode(raw)
    except ValueError:continue
    if q.op!=HELLO and (r.session!=q.session or r.tx!=q.tx):continue
    if accept and not accept(r):continue
    if q.op!=HELLO and r.state==FAILED:raise RuntimeError('device failed reason='+str(r.detail))
    if q.op!=HELLO and r.state!=APPLIED and r.detail:raise RuntimeError('device refused/busy reason='+str(r.detail))
    return r,raw
  raise TimeoutError('no matching board response')
 def hello(self):
  h,raw=self.exchange(Header());bundle=raw[64:].decode('ascii')
  if h.op!=REPLY or not re.fullmatch('[0-9a-f]{64}',bundle):raise ValueError('missing firmware source bundle')
  self.bundle=bundle;self.raw_hello=raw.hex();self.header=h;return h
 def request(self,p,input_id,image_crc):
  validate(1280,720,Rect(0,0,1280,720),p)
  h=self.hello()
  if h.tx==0xffffffff or input_id>0xffffffff:raise ValueError('ID exhausted; restart app')
  return Header(session=h.session,tx=h.tx+1,input_id=input_id,style=p.style,preset=p.preset,block=p.block,image_crc=image_crc)
 def finish(self,q,start,progress):
  q=dataclasses.replace(q,op=STATUS,offset=0);deadline=start+self.timeout
  while time.monotonic()<deadline:
   r,raw=self.exchange(q)
   if r.state==APPLIED:
    receipt=parse_applied(r,raw[64:],q);receipt.update(source_bundle=self.bundle,raw_hello_hex=self.raw_hello,peer=list(self.peer),raw_reply_hex=raw.hex(),pc_elapsed_seconds=time.monotonic()-start);self.header=r;return receipt
   progress('设备生成 / 等待显示退休');time.sleep(.02)
  raise TimeoutError('no APPLIED before deadline; query same session/tx before retrying')
 def upload_chunks(self,q,rgba,progress):
  pending={};next_offset=acked=sent=0;start=time.monotonic();maximum=0
  self.sock.settimeout(.02)
  try:
   while next_offset<len(rgba) or pending:
    if self.stop.is_set():raise InterruptedError('cancelled')
    now=time.monotonic()
    if now>=self.deadline:raise TimeoutError('upload deadline exceeded')
    while next_offset<len(rgba) and len(pending)<self.window:
     h=dataclasses.replace(q,op=DATA,offset=next_offset);raw=h.encode(rgba[next_offset:next_offset+1200])
     self.sock.sendto(raw,self.peer);pending[next_offset]=[raw,now,1];next_offset+=1200;sent+=1;maximum=max(maximum,len(pending))
    try:raw,peer=self.sock.recvfrom(1280)
    except socket.timeout:raw=None
    if raw is not None and peer==self.peer:
     try:r=Header.decode(raw)
     except ValueError:r=None
     if r is not None and r.op in (ACK,REPLY) and r.offset in pending and all(getattr(r,k)==getattr(q,k) for k in ('session','tx','input_id','style','preset','block','width','height','image_crc')):
      if r.state==FAILED:raise RuntimeError('device failed reason='+str(r.detail))
      if r.state!=APPLIED and r.detail:raise RuntimeError('device refused/busy reason='+str(r.detail))
      if r.op==ACK and r.state in (RECEIVING,READY,PROCESSING,APPLIED):
       del pending[r.offset];acked+=1
       if acked%40==0 or not pending and next_offset>=len(rgba):progress(f'上传 {min(100,acked*1200*100//len(rgba))}%')
    now=time.monotonic()
    for chunk in pending.values():
     if now-chunk[1]>=.08:
      if chunk[2]>=125:raise TimeoutError('chunk ACK retry budget exceeded')
      if self.stop.is_set():raise InterruptedError('cancelled')
      self.sock.sendto(chunk[0],self.peer);chunk[1]=now;chunk[2]+=1;sent+=1
  finally:self.sock.settimeout(.25)
  return dict(window=self.window,chunks=acked,datagrams_sent=sent,retransmissions=sent-acked,max_inflight=maximum,seconds=time.monotonic()-start)
 def send_image(self,prepared,p,progress=lambda x:None):
  start=time.monotonic();self.deadline=start+self.timeout;h=self.hello();q=self.request(p,h.input_id+1,zlib.crc32(prepared.rgba));q.op=BEGIN
  try:
   r,_=self.exchange(q,struct.pack('!4I',*dataclasses.astuple(prepared.rect)))
   upload=self.upload_chunks(q,prepared.rgba,progress)
   receipt=self.finish(q,start,progress)
  except (InterruptedError,TimeoutError):
   self.sock.sendto(dataclasses.replace(q,op=CANCEL,offset=0).encode(),self.peer);raise
  receipt.update(source_sha256=prepared.source_sha256,rect=dataclasses.asdict(prepared.rect),upload=upload);return receipt
 def select_style(self,input_id,image_crc,p,progress=lambda x:None):
  start=time.monotonic();self.deadline=start+self.timeout;q=self.request(p,input_id,image_crc);q.op=STYLE;self.exchange(q);return self.finish(q,start,progress)
PALETTE_NAMES=('冰蓝全息','青紫赛博','荧光绿','紫粉未来','琥珀流金','朱砂霓红','青铜流光')
STYLES=('原图对照','彩色海报','拓片版画','霓虹柔光','像素抖动')
class App:
 def __init__(self,root):
  import tkinter as tk
  from tkinter import ttk
  self.root=root;self.busy=False;self.stop=threading.Event();self.events=queue.Queue();self.prepared=None;self.active=None;self.worker=None;self.closing=False
  root.title('纹样重生 · Vortex 四风格图片工坊 · 原生720p加速版');root.geometry('780x560')
  self.board=tk.StringVar(value=DEFAULT_BOARD);self.style=tk.IntVar(value=1);self.palette=tk.StringVar(value=PALETTE_NAMES[1]);self.block=tk.StringVar(value='4');self.message=tk.StringVar(value='选图后点击生成；板端处理 1280×720')
  panel=ttk.Frame(root,padding=16);panel.pack(fill='both',expand=True)
  ttk.Label(panel,text='文物图片 → Vortex → miniDP',font=('Microsoft YaHei',17)).pack(anchor='w')
  row=ttk.Frame(panel);row.pack(fill='x',pady=10);ttk.Label(row,text='板卡 IP').pack(side='left');self.ip_entry=ttk.Entry(row,textvariable=self.board);self.ip_entry.pack(side='left',padx=8)
  self.open_button=ttk.Button(row,text='选择图片',command=self.open_image);self.open_button.pack(side='left')
  self.preview=ttk.Label(panel,text='等待选择文物图片');self.preview.pack(pady=8)
  row=ttk.Frame(panel);row.pack(fill='x');self.controls=[self.ip_entry,self.open_button]
  for i,name in enumerate(STYLES):
   b=ttk.Radiobutton(row,text=name,variable=self.style,value=i);b.pack(side='left',padx=3);self.controls.append(b)
  row=ttk.Frame(panel);row.pack(fill='x',pady=12);ttk.Label(row,text='海报 / 霓虹 / 像素配色').pack(side='left');c=ttk.Combobox(row,textvariable=self.palette,values=PALETTE_NAMES,state='readonly',width=12);c.pack(side='left',padx=8);self.controls.append(c)
  ttk.Label(row,text='像素块').pack(side='left');c=ttk.Combobox(row,textvariable=self.block,values=('2','4','8'),state='readonly',width=4);c.pack(side='left');self.controls.append(c)
  self.generate_button=ttk.Button(panel,text='在板卡生成 / 切换风格',command=self.generate);self.generate_button.pack(pady=10);self.controls.append(self.generate_button)
  ttk.Label(panel,textvariable=self.message,wraplength=730).pack(anchor='w');ttk.Label(panel,text='拓片固定黑金色；其他风格支持七套配色。已生成配色直接回切；新配色复用滤波结果。完成表示收到板端 APPLIED 回执；实屏仍需观察。',wraplength=730).pack(anchor='w',pady=8)
  root.protocol('WM_DELETE_WINDOW',self.close);root.after(80,self.drain)
 def set_busy(self,value):
  self.busy=value
  for c in self.controls:c.configure(state='disabled' if value else ('readonly' if c.winfo_class()=='TCombobox' else 'normal'))
 def open_image(self):
  from tkinter import filedialog
  from PIL import Image,ImageTk
  name=filedialog.askopenfilename(filetypes=[('图片','*.jpg *.jpeg *.png *.bmp *.webp')])
  if not name:return
  try:
   self.prepared=prepare_image(name);self.active=None
   image=Image.frombytes('RGBA',(1280,720),self.prepared.rgba);image.thumbnail((400,260));self.photo=ImageTk.PhotoImage(image);self.preview.configure(image=self.photo,text='');self.message.set('已选图；点击生成才上传')
  except Exception as e:self.message.set('选图失败：'+str(e))
 def generate(self):
  if self.busy:return
  if not self.prepared:self.message.set('请先选择图片');return
  style=self.style.get();p=Params(style,PALETTE_NAMES.index(self.palette.get()) if style in (1,3,4) else 0,int(self.block.get()) if style==4 else 0);board=self.board.get();prepared=self.prepared;active=self.active
  self.stop.clear();self.set_busy(True)
  def work():
   client=None
   try:
    client=Client(board,stop=self.stop);progress=lambda text:self.events.put(('progress',text))
    receipt=client.select_style(active['input_id'],active['input_crc'],p,progress) if active and active['peer'][0]==client.peer[0] else client.send_image(prepared,p,progress)
    receipt['source_sha256']=prepared.source_sha256
    out=S/'desktop/receipts';out.mkdir(exist_ok=True);path=out/f"{receipt['session']:08x}-{receipt['tx']}-{time.time_ns()}.json";path.write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8');self.events.put(('done',(receipt,path)))
   except Exception as e:self.events.put(('error',str(e)))
   finally:
    if client:client.close()
  self.worker=threading.Thread(target=work,daemon=False);self.worker.start()
 def drain(self):
  while not self.events.empty():
   kind,value=self.events.get()
   if kind=='progress':self.message.set(value)
   elif kind=='done':
    self.active,path=value;self.set_busy(False);m=self.active['metrics'];hz=m['timer_hz'];self.message.set(f"完成：{STYLES[self.active['style']]}，缓存 {m['cache_hit']}，生成 {m['generate_ticks']/hz:.3f}s；回执 {path.name}")
   else:self.active=None;self.set_busy(False);self.message.set('未确认完成：'+value)
  if self.closing and (self.worker is None or not self.worker.is_alive()):self.root.destroy();return
  self.root.after(80,self.drain)
 def close(self):
  self.stop.set();self.closing=True
  if self.busy:self.message.set('正在停止电脑端等待；设备已开始生成时仍会完成当前任务')
  elif self.worker is None or not self.worker.is_alive():self.root.destroy()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--gui',action='store_true');ap.add_argument('--window',type=int,choices=(1,4,8),default=8);ap.add_argument('--image',type=Path);ap.add_argument('--board',default=DEFAULT_BOARD);ap.add_argument('--port',type=int,default=5011);ap.add_argument('--style',type=int,choices=range(5),default=1);ap.add_argument('--preset',type=int,choices=range(7),default=1);ap.add_argument('--block',type=int,choices=(2,4,8),default=4);ap.add_argument('--receipt',type=Path,default=S/'desktop/last-receipt.json');a=ap.parse_args()
 if a.gui:
  import tkinter as tk
  root=tk.Tk();App(root);root.mainloop();return
 if not a.image:ap.error('use --gui or --image')
 p=Params(a.style,a.preset if a.style in (1,3,4) else 0,a.block if a.style==4 else 0);c=Client(a.board,a.port,window=a.window)
 try:r=c.send_image(prepare_image(a.image),p,print);a.receipt.parent.mkdir(parents=True,exist_ok=True);a.receipt.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8');print(a.receipt)
 finally:c.close()
if __name__=='__main__':main()
