#!/usr/bin/env python3
"""Controlled SSH-side client; no device open, startup or execution retry."""
import argparse,json,os,pathlib,socket,stat,struct,sys,time
LIMIT=65536
def pairs(items):
 result={}
 for k,v in items:
  if k in result:raise ValueError('duplicate JSON key')
  result[k]=v
 return result
def not_integer(_):raise ValueError('RPC numbers must be integers')
def load(raw):return json.loads(raw,object_pairs_hook=pairs,parse_float=not_integer,parse_constant=not_integer)
def main(argv=None):
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--socket',required=True);parser.add_argument('--timeout-ms',type=int,default=2000)
 commands=parser.add_subparsers(dest='command');commands.required=True
 for command in ('models','scenes','effects','inputs','results','input-register','submit','status','recover','cancel','result','display','export','reap','mode','mode-status'):
  sub=commands.add_parser(command)
  if command in ('mode','mode-status','inputs','results'):sub.add_argument('--boot-id',required=True)
  elif command not in ('models','scenes','effects'):
   for key in ('boot-id','client-id','request-id'):sub.add_argument('--'+key,required=True)
  if command in ('submit','input-register'):
   sub.add_argument('--entry-id',required=True);sub.add_argument('--input-id',required=True);sub.add_argument('--parameters',default='{}')
  if command in ('inputs','results'):sub.add_argument('--entry-id',required=True)
  if command in ('display','export'):sub.add_argument('--artifact',required=True,type=int)
  if command=='export':sub.add_argument('--destination-id',required=True)
  if command=='mode':sub.add_argument('--mode',required=True,choices=('nn','image','p8'))
 args=parser.parse_args(argv)
 if not 1<=args.timeout_ms<=30000:parser.error('timeout-ms outside1..30000')
 try:
  request=dict(version=1,command=args.command)
  for k in ('boot_id','client_id','request_id','entry_id','input_id','artifact','destination_id','mode'):
   if hasattr(args,k):request[k]=getattr(args,k)
  if args.command in ('submit','input-register'):request['parameters']=load(args.parameters)
  raw=json.dumps(request,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('utf8')
  if not 0<len(raw)<=LIMIT:raise ValueError('request exceeds64KiB')
  path=pathlib.Path(args.socket);directory=path.parent.lstat();target=path.lstat()
  if not path.is_absolute() or not stat.S_ISDIR(directory.st_mode) or directory.st_uid!=os.geteuid() or stat.S_IMODE(directory.st_mode)!=0o700:raise ValueError('private same uid0700 directory required')
  if not stat.S_ISSOCK(target.st_mode) or target.st_uid!=os.geteuid() or stat.S_IMODE(target.st_mode)!=0o600:raise ValueError('private same uid0600 socket required')
  observation_seconds=args.timeout_ms/1000.0;deadline=time.monotonic()+observation_seconds
  with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as peer:
   peer.settimeout(observation_seconds);peer.connect(str(path))
   _,uid,_=struct.unpack('3i',peer.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
   if uid!=os.geteuid():raise ValueError('server uid differs')
   peer.sendall(struct.pack('!I',len(raw))+raw)
   def exact(n):
    output=bytearray()
    while len(output)<n:
     remaining=deadline-time.monotonic()
     if remaining<=0:raise TimeoutError('bounded RPC deadline')
     peer.settimeout(remaining);part=peer.recv(n-len(output))
     if not part:raise ValueError('truncated response; request status must be queried, no retry')
     output.extend(part)
    return bytes(output)
   length=struct.unpack('!I',exact(4))[0]
   if not 0<length<=LIMIT:raise ValueError('response exceeds64KiB')
   response=load(exact(length).decode('utf8'))
   if not isinstance(response,dict) or response.get('version')!=1 or type(response.get('ok')) is not bool or not isinstance(response.get('boot_id'),str):raise ValueError('invalid response envelope')
   expected={'version','ok','boot_id','result' if response['ok'] else 'error'}
   if set(response)!=expected:raise ValueError('invalid response fields')
   if 'boot_id' in request and response['boot_id']!=request['boot_id'] and response['ok']:raise ValueError('unexpected successful boot epoch')
   print(json.dumps(response,sort_keys=True,separators=(',',':')))
   return 0 if response['ok'] else 1
 except (OSError,ValueError,TypeError,RecursionError) as e:
  print('vxgpuctl: '+str(e)+'; no automatic resubmit',file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
