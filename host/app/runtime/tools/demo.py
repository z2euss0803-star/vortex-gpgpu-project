#!/usr/bin/python3
"""Serial menu client for one already-running S8 service."""
import argparse, json, os, pathlib, subprocess, sys, time
def clock_ns():return time.monotonic_ns() if hasattr(time,'monotonic_ns') else int(time.monotonic()*1000000000)

class Client:
 def __init__(self,socket_path,ctl='/usr/bin/vxgpuctl',client_id='serial-demo'):
  self.socket=socket_path;self.ctl=ctl;self.client_id=client_id;self.boot=''
  self.boot=self.call('models',discovery=True)['boot_id']
 def call(self,command,discovery=False,**values):
  argv=[self.ctl,'--socket',self.socket,'--timeout-ms','30000',command]
  if command not in ('models','scenes','effects'):
   argv+=['--boot-id',self.boot]
   if command not in ('mode','mode-status','inputs','results'):
    argv+=['--client-id',self.client_id]
  for key,value in values.items():argv+=['--'+key.replace('_','-'),str(value)]
  run=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=35)
  if run.returncode not in (0,1):raise OSError(run.stderr.decode('utf8','replace')[-1000:])
  response=json.loads(run.stdout.decode('utf8'))
  if response.get('version')!=1 or not response.get('ok'):raise RuntimeError(str(response.get('error',response)))
  if self.boot and response.get('boot_id')!=self.boot:raise RuntimeError('启动身份已变化，请重新打开菜单')
  return response if discovery else response['result']

def save(path,record):
 temp=path.with_name(path.name+'.tmp')
 with temp.open('w',encoding='utf8') as stream:
  json.dump(record,stream,ensure_ascii=False,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
 temp.chmod(0o600);os.replace(str(temp),str(path))
 # Commit the rename before a completed client result can be reaped.
 sync_directory(path.parent)

def sync_directory(path):
 if hasattr(os,'O_DIRECTORY'):
  directory=os.open(str(path),os.O_RDONLY|os.O_DIRECTORY)
  try:os.fsync(directory)
  finally:os.close(directory)

def private_directory(path,exist_ok=False):
 # fsync(file)+fsync(session) is insufficient if session/multichar itself is
 # new. Commit each newly created ancestor entry before SUBMIT or REAP.
 path=pathlib.Path(path)
 path.mkdir(parents=True,mode=0o700,exist_ok=exist_ok)
 # exists() is not proof of durability: a prior failed fsync may have left
 # these directories present. Recommit the full ancestry on each independent
 # attempt, before any request is submitted. This never retries a SUBMIT.
 for parent in reversed(path.absolute().parents):sync_directory(parent)
 return path

def select_mode(client,target,timeout=300,interval=.2):
 deadline=time.monotonic()+timeout;state=client.call('mode',mode=target)
 while state.get('current')!=target or state.get('switching') is not False:
  if time.monotonic()>=deadline:raise TimeoutError('应用仍在安全排空；未提交新的计算请求')
  time.sleep(interval);state=client.call('mode-status')
 return state

def run_request(client,entry,input_id,store,timeout=300,interval=.2,*,display=True,progress=None):
 """Submit once. Persist the original identity before crossing the socket."""
 start=clock_ns();mode='p8' if entry.startswith('p8-') else 'nn'
 select_mode(client,mode,timeout,interval)
 store=pathlib.Path(store);private_directory(store,exist_ok=True)
 request_id='demo-'+os.urandom(16).hex();path=store/(request_id+'.json')
 record=dict(request_id=request_id,boot_id=client.boot,client_id=client.client_id,
  entry_id=entry,input_id=input_id,status='SUBMIT_INTENT',begin_ns=start,gpu_raw_cycles=None)
 save(path,record)
 try:
  # Transport failure may follow a successful SUBMIT. No branch retries it.
  record['submit']=client.call('submit',request_id=request_id,entry_id=entry,input_id=input_id,parameters='{}')
  deadline=time.monotonic()+timeout
  while True:
   state=client.call('status',request_id=request_id);record['observation']=state;record['status']=state['status']
   if progress:progress(state)
   if state['status'] in ('FAILED','CANCELLED'):break
   if state['status']=='COMPLETED':
    record['completion_observed_ns']=clock_ns()
    record['result']=client.call('result',request_id=request_id)
    if not record['result'].get('execution_complete'):raise RuntimeError('缺少完整计算结果')
    if entry=='p8-control':
     scene=json.loads(record['result'].get('raw_legacy_json','{}'));record['scene']=scene
     record['status']='COMMAND_APPLIED' if scene.get('applied') is True and scene.get('command_error')==0 else 'COMMAND_REJECTED'
    elif entry=='p8-texture':
     record['command_status']='COMPLETED'
     while True:
      texture=client.call('export',request_id=request_id,artifact=0,destination_id='texture-status-v1');record['texture']=texture
      if texture.get('phase')=='APPLIED' and texture.get('applied') is True:record['status']='APPLIED';break
      if texture.get('phase') in ('FAILED','CANCELLED','SUPERSEDED'):record['status']=texture['phase'];break
      if time.monotonic()>=deadline:record['status']='UNKNOWN';record['texture_timeout']=True;break
      time.sleep(interval)
    elif display and not entry.startswith('p8-'):
     try:record['display']=client.call('display',request_id=request_id,artifact=0)
     except (OSError,RuntimeError,subprocess.SubprocessError) as error:record['display_error']=str(error)
    break
   if time.monotonic()>=deadline:
    record['status']='UNKNOWN';record['observation_timeout']=True;break
   time.sleep(interval)
 except BaseException as error:
  record['status']='UNKNOWN';record['error']=str(error);raise
 finally:
  record['end_ns']=clock_ns();record['wall_ns']=record['end_ns']-start;save(path,record)
 return record

def choose(rows,key='input_id',prompt='请选择编号（0 返回）：'):
 for index,row in enumerate(rows,1):print('  %d  %s'%(index,row[key]))
 answer=input(prompt).strip()
 if not answer or answer=='0':return None
 if not answer.isdigit() or not 1<=int(answer)<=len(rows):raise ValueError('请选择列表内编号')
 return rows[int(answer)-1]

def report(record):
 print('状态：'+record['status']+'；本次用时 %.3f 秒'%(record['wall_ns']/1e9))
 print('结果编号：'+record['request_id'])
 if record.get('observation',{}).get('device_pending'):print('设备尚未回收，当前资源继续保留。')
 result=record.get('result',{})
 if result:
  print('数值状态：'+result.get('numeric_class','UNKNOWN'))
  if result.get('gpu_raw_cycles') is not None:print('GPU 原始周期：'+str(result['gpu_raw_cycles']))
  try:
   metadata=json.loads(result.get('raw_legacy_json','{}'))
   if metadata.get('top_candidates'):print('识别候选（类别编号/分数）：'+json.dumps(metadata['top_candidates'],ensure_ascii=False))
   elif metadata.get('topk_class_ids'):print('识别候选（类别编号/原始logit）：'+str(list(zip(metadata['topk_class_ids'],metadata['topk_raw_logits']))))
  except (ValueError,TypeError):pass
 if record.get('display_error'):print('计算已完成，显示尚未确认：'+record['display_error'])
 if record.get('scene'):
  scene=record['scene'];print('三维状态：'+json.dumps({k:scene[k] for k in ('object','pose','paused','remaining','startup_remaining','have_displayed','command_error') if k in scene},ensure_ascii=False))
  if record['status']=='COMMAND_APPLIED':print('控制已应用；实际帧呈现按三维完成回执记录。')
 if record.get('texture'):print('纹理状态：'+record['texture'].get('phase','UNKNOWN'))
 if record.get('error'):print(record['error'])

def nn(client,kind,store):
 models=client.call('models')['entries'];matched=[x for x in models if (x['entry_id']=='hunet' if kind=='hunet' else kind in x['entry_id'].lower())]
 if not matched:raise ValueError('当前系统未安装此模型')
 model=matched[0] if len(matched)==1 else choose(matched,'entry_id')
 if not model:return
 rows=client.call('inputs',entry_id=model['entry_id'])['inputs']
 if kind=='hunet':rows=[row for row in rows if not row['input_id'].startswith(('example-','reviewed-'))]
 if not rows:raise ValueError('暂无输入，请先使用 电脑端图片工具上传')
 selected=choose(rows)
 if selected:report(run_request(client,model['entry_id'],selected['input_id'],store))

def registered_textures(client):
 scenes=client.call('scenes')['entries']
 if not any(row['entry_id']=='p8-texture' for row in scenes):return []
 return client.call('inputs',entry_id='p8-texture')['inputs']

def p8(client,store):
 select_mode(client,'p8')
 print('三维展示已选择。可用默认纹理、已有风格化结果，或从 电脑端上传普通图片。')
 print('电脑端统一工具：vxgpu_input.py texture 图片 --board 板端IP；等待 APPLIED。')
 textures=registered_textures(client)
 keys={'1':'花瓶','2':'纸袋','3':'兵马俑','p':'旋转/暂停','s':'停止新增帧','r':'100帧旋转','l':'持续旋转','f':'正面','a':'左面','d':'右面','b':'背面','q':'左前','e':'右前','z':'左后','c':'右后','g':'自动八面','i':'查看三维状态','u':'选择电脑端图片/已有风格化图','m':'选择已完成Mosaic结果'}
 if textures:keys['t']='选择已注册纹理'
 while True:
  print('  '.join(k+' '+v for k,v in keys.items()));key=input('三维操作（0 返回）：').strip()
  if key=='0':return
  if key=='t':
   if not textures:print('暂无已注册文件纹理，请选择 u 或 m。');continue
   selected=choose(textures)
   if selected:report(run_request(client,'p8-texture',selected['input_id'],store))
  elif key=='u':print('在 电脑端运行统一图片工具的 texture --gui，选择已有风格化图片或普通图片，等待 APPLIED。')
  elif key=='m':
   models=[x for x in client.call('models')['entries'] if 'mosaic' in x['entry_id'].lower()]
   if not models:print('暂无 Mosaic 模型与结果。');continue
   rows=client.call('results',entry_id=models[0]['entry_id'])['results']
   selected=choose([dict(row,input_id=row['input_id']+' / '+row['request_id']) for row in rows])
   if selected:
    selected['input_id']=next(row['input_id'] for row in rows if row['request_id']==selected['request_id'] and row['client_id']==selected['client_id'])
    store=pathlib.Path(store);private_directory(store,exist_ok=True);save(store/'texture-selection.json',selected)
    print('已选定真实 Mosaic 结果。电脑端统一工具使用 result-to-3d --selected 执行文件转换、传输与呈现；不会重新计算 Mosaic。')
  elif key in keys:report(run_request(client,'p8-control',key,store))
  else:print('请选择列出的操作。')


def load_hunet_pages(installation,entry_id="hunet"):
 import hashlib,stat,struct
 def read_sealed(path,limit,pin=None):
  path=pathlib.Path(path);before=path.lstat()
  if not stat.S_ISREG(before.st_mode) or not 0<before.st_size<=limit:raise ValueError('示例目录文件类型或大小不符')
  fd=os.open(str(path),os.O_RDONLY|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_BINARY',0))
  with os.fdopen(fd,'rb') as stream:raw=stream.read(limit+1);after=os.fstat(stream.fileno())
  if (before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns) or len(raw)!=before.st_size:raise ValueError('示例目录在读取期间变化')
  if pin and hashlib.sha256(raw).hexdigest()!=pin:raise ValueError('示例目录身份不符')
  return raw
 installation=pathlib.Path(installation);pin=read_sealed(installation.with_suffix('.sha256'),65).decode('ascii').strip();data=json.loads(read_sealed(installation,65536,pin))
 models=[row for row in data['nn'] if row.get('kind')=='hunet' and row.get('presets') and row.get('id')==entry_id]
 if len(models)!=1:raise ValueError('当前镜像尚未安装整页示例；单字入口仍可使用')
 spec=models[0];preset=spec['presets'];directory=pathlib.Path(preset['directory']);raw=read_sealed(directory/'manifest.json',65536,preset['sha256']);catalog=json.loads(raw)
 if read_sealed(directory/'manifest.sha256',65).decode('ascii').strip()!=preset['sha256'] or (catalog.get('format'),catalog.get('version')) not in (('hunet-fixed-inputs-v1',1),('hunet-fixed-inputs-v2',2)):raise ValueError('固定示例目录格式或封印不符')
 pages=catalog['pages'];items=[item for page in pages for item in page['items']]
 if not 1<=len(pages)<=8 or not 1<=len(items)<=128 or len({x['input_id'] for x in items})!=len(items):raise ValueError('整页示例数量或编号不符')
 for item in items:
  if item['input_id']!='example-'+item['roi_id'] or item['tensor_bytes']!=196608:raise ValueError('固定字框输入编号或长度不符')
 view=data['display']['hunet'];vdir=pathlib.Path(view['directory']);manifest=json.loads(read_sealed(vdir/'manifest.json',65536,view['sha256']));label=next(row for row in manifest['files'] if row['path']=='labels.bin')
 raw=read_sealed(vdir/'labels.bin',259456,label['sha256'])
 if len(raw)!=259456 or raw[:8]!=b'DMLAB1\0\0':raise ValueError('8105类别字表身份/长度不符')
 labels={}
 for index in range(8105):
  class_id,codepoint=struct.unpack_from('<II',raw,96+index*32)
  if class_id!=index or not 0<codepoint<=0x10ffff or 0xd800<=codepoint<=0xdfff:raise ValueError('类别字表条目不符')
  labels[index]=chr(codepoint)
 return spec,pages,labels

def run_hunet_page(client,model,page,store,labels,timeout=300,interval=.2):
 import math
 items=page['items']
 if not 1<=len(items)<=128 or len({x['input_id'] for x in items})!=len(items):raise ValueError('独立字框数量或编号不符')
 available={row['input_id']:row for row in client.call('inputs',entry_id=model['entry_id'])['inputs']}
 for item in items:
  current=available.get(item['input_id'])
  if not current or current['sha256']!=item['tensor_sha256'] or current['bytes']!=196608 or not current.get('plan'):raise ValueError('服务未提供匹配的固定示例输入：'+item['input_id'])
 base=pathlib.Path(store)/'multichar'/('page-'+os.urandom(16).hex());private_directory(base);path=base/'summary.json';begin=clock_ns()
 summary=dict(format='hunet-reviewed-page-result-v1',boot_id=client.boot,client_id=client.client_id,entry_id=model['entry_id'],model=model['model'],page_id=page['id'],title=page['title'],order='REVIEWED_BOX_MANIFEST_ORDER_NOT_TRANSCRIPTION',planned_inputs=[x['input_id'] for x in items],characters=[],status='RUNNING',numeric_class='NOT_EVALUATED',summary_path=str(path),begin_ns=begin,predicted_text='')
 save(path,summary);previous=None
 for item in items:
  child_dir=base/'requests'/item['roi_id'];entry=dict(roi_id=item['roi_id'],input_id=item['input_id'],tensor_sha256=item['tensor_sha256'],plan=available[item['input_id']]['plan'],status='PREPARING',record_directory=str(child_dir));summary['characters'].append(entry);save(path,summary)
  try:
   record=run_request(client,model['entry_id'],item['input_id'],child_dir,timeout,interval)
   entry.update(request_id=record['request_id'],status=record['status'],wall_ns=record['wall_ns'],numeric_class=record.get('result',{}).get('numeric_class','UNKNOWN'),record_path=str(child_dir/(record['request_id']+'.json')))
   state=record.get('observation',{});result=record.get('result',{})
   if record['status']!='COMPLETED' or state.get('device_pending') or state.get('retirement_unknown'):raise ValueError('当前字请求未正常完成/退休，停止新增字请求')
   if not result.get('execution_complete') or result.get('model')!=model['model'] or result.get('plan')!=entry['plan']:raise ValueError('当前字结果模型或计划身份不符')
   artifact=result['artifacts'][0]
   if artifact['bytes']!=32420 or artifact['layout']!='CHW_F32' or artifact['shape']!=[8105,1,1]:raise ValueError('当前字最终输出形状不符')
   raw=json.loads(result['raw_legacy_json']);ids=raw['topk_class_ids'];logits=raw['topk_raw_logits']
   if not 1<=len(ids)<=5 or len(ids)!=len(logits) or any(type(x) is not int or x not in labels for x in ids) or any(type(v) not in (int,float) or not math.isfinite(v) for v in logits):raise ValueError('当前字类别/有限logit不符')
   entry.update(topk_class_ids=ids,topk_raw_logits=logits,score_kind='RAW_LOGIT_NOT_PROBABILITY',predicted_text=labels[ids[0]],output_sha256=artifact.get('sha256'),display=record.get('display'),display_error=record.get('display_error'))
   print('%s  %s  %.3f秒  %s'%(item['roi_id'],entry['predicted_text'],entry['wall_ns']/1e9,entry['numeric_class']))
   # A newly presented frame can retire the previous result's DP consumer.
   # Reaping is the ordinary service operation; no hardware ACK/reset is invented.
   if previous:previous['reap']=client.call('reap',request_id=previous['request_id'])
   previous=entry if not record.get('display_error') else None
   if previous is None:entry['reap']=client.call('reap',request_id=entry['request_id'])
  except BaseException as error:
   saved=sorted(child_dir.glob('demo-*.json')) if child_dir.exists() else []
   if saved:
    record=json.loads(saved[-1].read_text(encoding='utf8'));entry.update(request_id=record['request_id'],status=record['status'],record_path=str(saved[-1]))
   entry['error']=str(error);summary['status']='UNKNOWN' if entry['status'] in ('UNKNOWN','PREPARING') or isinstance(error,KeyboardInterrupt) else 'STOPPED';summary['error']=str(error);break
  finally:
   summary['predicted_text']=''.join(x.get('predicted_text','') for x in summary['characters']);summary['end_ns']=clock_ns();summary['wall_ns']=summary['end_ns']-begin;save(path,summary)
 else:summary['status']='COMPLETED'
 summary['end_ns']=clock_ns();summary['wall_ns']=summary['end_ns']-begin;save(path,summary)
 print('整页状态：'+summary['status']+'；按字框编号汇总预测：'+summary['predicted_text']);print('汇总记录：'+str(path));print('整页准确率：尚未评估；类别分数为原始logit。')
 return summary


def hunet_page_session_identity(base_plan,page):
 import hashlib,re
 if not re.fullmatch('[0-9a-f]{64}',base_plan):raise ValueError('基础计划身份不符')
 items=page['items']
 if not 1<=len(items)<=128 or len({x['input_id'] for x in items})!=len(items):raise ValueError('固定整页字框数量或编号不符')
 raw='VX_HUNET_PAGE_INPUT_V1:'+str(len(items))+':'
 def text(value):return str(len(value.encode('utf8')))+':'+value
 policy='HUNET_REVIEWED_P0_FROZEN_V1';policy_sha=hashlib.sha256(policy.encode()).hexdigest()
 for i,item in enumerate(items):
  if not re.fullmatch('[A-Za-z0-9_.-]{1,128}',item['input_id']):raise ValueError('字框输入ID不符')
  for key in ('source_sha256','tensor_sha256'):
   if not re.fullmatch('[0-9a-f]{64}',item[key]) or item[key]=='0'*64:raise ValueError('固定字框身份不符')
  source_format=item.get('source_format','PNG')
  if source_format!='PNG':raise ValueError('审阅字框必须为冻结PNG')
  raw+=str(i)+':'+''.join(text(x) for x in (item['input_id'],source_format,policy,item['source_sha256'],item['tensor_sha256'],policy_sha,'CHW_F32','FP32'))+'3:128:128:'
 input_sha=hashlib.sha256(raw.encode()).hexdigest();plan=hashlib.sha256(('VX_HUNET_PAGE_PLAN_V1:'+base_plan+':'+input_sha).encode()).hexdigest()
 return plan,input_sha

def run_hunet_page_session(client,model,page,store,labels,timeout=3600,interval=.2):
 """One admitted page source; frozen boxes, weights and arena shared by words."""
 import math,re
 plan,input_sha=hunet_page_session_identity(model['plan'],page);input_id='reviewed-'+page['id'];items=page['items']
 available={x['input_id']:x for x in client.call('inputs',entry_id=model['entry_id'])['inputs']};selected=available.get(input_id)
 if not selected or selected['plan']!=plan or selected['sha256']!=input_sha or selected['bytes']!=len(items)*196608:raise ValueError('服务未提供匹配的整页会话；请使用含整页资产的新镜像')
 base=pathlib.Path(store)/'multichar'/('session-'+os.urandom(16).hex());private_directory(base);path=base/'summary.json';begin=clock_ns()
 summary=dict(format='hunet-reviewed-page-session-v1',page_id=page['id'],title=page['title'],boot_id=client.boot,client_id=client.client_id,entry_id=model['entry_id'],model=model['model'],plan=plan,input_sha256=input_sha,order='REVIEWED_MANIFEST_ORDER',score_kind='RAW_LOGIT_NOT_PROBABILITY',numeric_class='NOT_EVALUATED',status='RUNNING',characters=[],predicted_text='',summary_path=str(path),begin_ns=begin)
 save(path,summary)
 eligible_for_reap=False
 def progress(state):
  p=state.get('application_progress',{})
  if p and p.get('total_words')==len(items):
   complete=p.get('completed_words')
   if type(complete) is not int or not 0<=complete<=len(items):raise ValueError('整页进度越界')
   if complete<summary.get('progress',{}).get('completed_words',0):raise ValueError('整页进度倒退')
   if summary.get('progress')!=p:summary['progress']=p;save(path,summary);print('整页完成 %d/%d'%(complete,len(items)))
 try:
  record=run_request(client,model['entry_id'],input_id,base/'request',timeout,interval,display=False,progress=progress);summary.update(request_id=record['request_id'],status=record['status'],request_record=str(base/'request'/(record['request_id']+'.json')))
  if record['status']!='COMPLETED':return summary
  result=record.get('result',{});observation=record.get('observation',{})
  if observation.get('device_pending') or observation.get('retirement_unknown') or not result.get('execution_complete') or result.get('model')!=model['model'] or result.get('plan')!=plan:raise ValueError('整页未正常完成或身份不符')
  artifact=result['artifacts'][0]
  if artifact['layout']!='HUNET_PAGE_F32' or artifact['shape']!=[len(items),8105,1] or artifact['bytes']!=len(items)*32420:raise ValueError('整页logits输出形状不符')
  raw=json.loads(result['raw_legacy_json']);rows=raw['words']
  if raw.get('format')!='native-hunet-page-result-v1' or raw.get('page_id')!=page['id'] or raw.get('completed')!=len(items) or raw.get('total')!=len(items) or raw.get('order')!='REVIEWED_MANIFEST_ORDER' or raw.get('score_kind')!='RAW_LOGIT_NOT_PROBABILITY' or len(rows)!=len(items):raise ValueError('整页结果记录不完整')
  checked=[]
  for i,(item,row) in enumerate(zip(items,rows)):
   ids=row['topk_class_ids'];scores=row['topk_raw_logits'];output_sha=row['logits_sha256']
   if row['index']!=i or row['input_id']!=item['input_id'] or len(ids)!=5 or len(set(ids))!=5 or len(scores)!=5 or any(type(x) is not int or x not in labels for x in ids) or any(type(v) not in (float,int) or not math.isfinite(v) for v in scores) or not re.fullmatch('[0-9a-f]{64}',output_sha):raise ValueError('整页字框与结果映射不符')
   if any(scores[k]<scores[k+1] or (scores[k]==scores[k+1] and ids[k]>ids[k+1]) for k in range(4)):raise ValueError('TopK排序不符')
   checked.append(dict(index=i,roi_id=item['roi_id'],input_id=item['input_id'],tensor_sha256=item['tensor_sha256'],topk_class_ids=ids,topk_raw_logits=scores,predicted_text=labels[ids[0]],output_sha256=output_sha,bundle_artifact=artifact.get('path'),bundle_offset=i*32420,bytes=32420,numeric_class=result.get('numeric_class','UNKNOWN')))
  summary.update(characters=checked,predicted_text=''.join(x['predicted_text'] for x in checked),performance=raw.get('performance',{}),numeric_class=result.get('numeric_class','UNKNOWN'))
  # Final display is requested after every word/result binding has been checked.
  # A display error preserves a completed inference result and its evidence.
  summary['display_begin_ns']=clock_ns()
  try:summary['display']=client.call('display',request_id=record['request_id'],artifact=0)
  except (OSError,RuntimeError,subprocess.SubprocessError) as error:summary['display_error']=str(error)
  summary['display_end_ns']=clock_ns();summary['status']='COMPLETED'
  eligible_for_reap=True
 except BaseException as error:
  summary['error']=str(error);summary['status']='STOPPED' if isinstance(error,ValueError) else 'UNKNOWN'
  records=sorted((base/'request').glob('demo-*.json')) if (base/'request').exists() else []
  if records:
   record=json.loads(records[-1].read_text(encoding='utf8'));summary['request_id']=record['request_id'];summary['request_record']=str(records[-1]);progress(record.get('observation',{}))
 finally:
  summary['end_ns']=clock_ns();summary['wall_ns']=summary['end_ns']-begin;save(path,summary)
  if eligible_for_reap:
   # The final validated summary and original request are durable first. Reap
   # drops only this client's hold; ResultStore preserves every live DP lease.
   # A lost reply remains recorded and is never automatically retried.
   summary['client_reap_begin_ns']=clock_ns()
   try:summary['client_reap']=client.call('reap',request_id=record['request_id'])
   except (OSError,RuntimeError,subprocess.SubprocessError) as error:summary['client_reap_error']=str(error)
   summary['client_reap_end_ns']=clock_ns();summary['end_ns']=clock_ns();summary['wall_ns']=summary['end_ns']-begin;save(path,summary)
  print('整页状态：'+summary['status']+'；按审阅顺序汇总预测：'+summary['predicted_text']);print('汇总记录：'+str(path));print('分数为原始logit；整页准确率尚未评估。')
 return summary

def hunet(client,store,installation):
 while True:
  print('1 单字识别   2 整页快速会话（权重/工作区复用，结束显示）   3 逐字演示（兼容独立请求）   4 CPU统计节点9候选整页   5 CPU统计全16候选整页   0 返回')
  option=input('古文字识别：').strip()
  if option=='0':return
  if option=='1':nn(client,'hunet',store)
  elif option in ('2','3','4','5'):
   candidate='hunet-gn9' if option=='4' else 'hunet-gn16' if option=='5' else 'hunet'
   if option in ('4','5'):print('独立CPU统计候选：A53数值与性能尚待验证，失败不会自动切换或重跑。')
   spec,pages,labels=load_hunet_pages(installation,candidate);models=[row for row in client.call('models')['entries'] if row['entry_id']==spec['id']]
   if len(models)!=1 or models[0]['model']!=spec['model']:raise ValueError('服务与已安装HUNet模型身份不符')
   rows=[dict(page,title='%s（%d字，按审阅字框编号）'%(page['title'],len(page['items']))) for page in pages];page=choose(rows,'title')
   if page:
    if option in ('2','4','5'):run_hunet_page_session(client,models[0],page,store,labels)
    else:
     print('兼容演示：每字独立请求与DP呈现；此模式不提供跨字权重驻留。')
     run_hunet_page(client,models[0],page,store,labels)
  else:print('请选择列表内编号。')

def main(argv=None):
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--socket',default='/run/vxgpud/control.sock');p.add_argument('--ctl',default='/usr/bin/vxgpuctl');p.add_argument('--state-dir',default='/run/vxgpud/menu');p.add_argument('--installation',default='/etc/vxgpud/installation.json');args=p.parse_args(argv)
 try:client=Client(args.socket,args.ctl)
 except (OSError,RuntimeError,ValueError,subprocess.SubprocessError) as error:
  print('设备服务暂不可用：'+str(error),file=sys.stderr);return 1
 store=pathlib.Path(args.state_dir)
 while True:
  print('\n1 古文字识别   2 Mosaic风格化   3 三维纹理展示\n4 查看已上传图片   5 查看本次结果/错误   0 退出菜单')
  try:
   option=input('请选择：').strip()
   if option=='0':return 0
   if option=='1':hunet(client,store,args.installation)
   elif option=='2':nn(client,'mosaic',store)
   elif option=='3':p8(client,store)
   elif option=='4':
    for model in client.call('models')['entries']:
     print(model['entry_id']+'：'+', '.join(i['input_id'] for i in client.call('inputs',entry_id=model['entry_id'])['inputs']))
   elif option=='5':
    for batch_path in sorted(store.glob('multichar/*/summary.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:8]:
     batch=json.loads(batch_path.read_text(encoding='utf8'));print(batch['title']+' '+batch['status']+' '+batch['predicted_text']+' '+str(batch_path))
    rows=[]
    for path in sorted(store.glob('demo-*.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:32]:
     record=json.loads(path.read_text(encoding='utf8'));rows.append(dict(input_id=record['request_id']+' '+record['entry_id']+' '+record['status'],record=record))
    selected=choose(rows)
    if selected:
     record=selected['record']
     if record['boot_id']==client.boot:
      try:record['observation']=client.call('status',request_id=record['request_id']);record['status']=record['observation']['status']
      except (OSError,RuntimeError,subprocess.SubprocessError) as error:record['query_error']=str(error)
     report(record)
     if record.get('observation',{}).get('device_pending') and input('输入 r 尝试一次安全回收，直接回车返回：').strip()=='r':
      print(json.dumps(client.call('recover',request_id=record['request_id']),ensure_ascii=False))
   else:print('请选择列表内编号。')
  except EOFError:return 0
  except KeyboardInterrupt:print('\n已返回菜单；原请求身份保留，可在结果中继续查询。')
  except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:print('操作未确认：'+str(error)+'；没有重复提交计算请求。')

if __name__=='__main__':raise SystemExit(main())
