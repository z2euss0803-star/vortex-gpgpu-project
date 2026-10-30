"""Fresh Windows/active-WSL inventory. Unknown never means idle."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import math
import platform
import re
import subprocess
import psutil
import sys
if __package__ in (None,''):sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.resource_policy import resource_policy


def assess_resources(snapshot):
    s=dict(snapshot)
    policy=resource_policy()
    s['resource_policy']=policy
    known=s.get('query_known') is True
    for key in ['physical_available_gib','commit_headroom_gib','other_heavy']:
        value=s.get(key)
        if not isinstance(value,(int,float)) or not math.isfinite(value) or value<0:
            known=False
    if not known:s['status']='UNKNOWN'
    elif str(s.get('host','')).upper() not in ['RY','DESKTOP-O7FQKAL']:s['status']='REFUSED'
    elif s['physical_available_gib']<policy['physical_min_gib'] or s['commit_headroom_gib']<policy['commit_min_gib'] or s['other_heavy']!=0:s['status']='REFUSED'
    else:s['status']='READY'
    return s


def resource_gate(root, stage):
    row={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.node(),
         'stage':stage,'query_known':True,'physical_available_gib':None,'commit_headroom_gib':None,
         'other_heavy':0,'heavy':[],'unknown':[],'wsl':[]}
    q=subprocess.run(['powershell.exe','-NoProfile','-Command',
       "$ErrorActionPreference='Stop'; $m=Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory; [pscustomobject]@{physical=$m.AvailableBytes;commit=($m.CommitLimit-$m.CommittedBytes)} | ConvertTo-Json -Compress"],capture_output=True,text=True)
    try:
        if q.returncode:raise ValueError(q.stderr)
        data=json.loads(q.stdout)
        row['physical_available_gib']=data['physical']/2**30
        row['commit_headroom_gib']=data['commit']/2**30
    except (ValueError,TypeError,KeyError) as error:row['unknown'].append('Windows memory query: '+str(error))
    heavy_names={'vivado.exe','xsim.exe','xsimk.exe','xelab.exe','xvlog.exe','xvhdl.exe','vvp.exe',
                 'verilator.exe','yosys.exe','nextpnr.exe','cc1plus.exe'}
    for p in psutil.process_iter(['pid','name']):
        if (p.info['name'] or '').lower() in heavy_names:
            try:row['heavy'].append({'pid':p.pid,'name':p.name(),'created':p.create_time(),
                'command':p.cmdline(),'cwd':p.cwd(),'cpu_seconds':sum(p.cpu_times()[:2]),'rss':p.memory_info().rss})
            except psutil.Error as error:row['unknown'].append(str(error))
    q=subprocess.run(['wsl.exe','--list','--running','--quiet'],capture_output=True)
    row['wsl_list_exit']=q.returncode
    if q.returncode:
        absence=subprocess.run(['powershell.exe','-NoProfile','-Command',
            "$ErrorActionPreference='Stop'; Get-CimInstance Win32_OptionalFeature -Filter \"Name='Microsoft-Windows-Subsystem-Linux'\" | Select-Object Name,InstallState | ConvertTo-Json -Compress"],capture_output=True,text=True)
        try:
            feature=json.loads(absence.stdout)
            row['wsl_feature']=feature
            if absence.returncode or feature.get('InstallState')!=2:
                row['unknown'].append('WSL running list failed; absence not proven: '+q.stderr.hex())
        except (ValueError,AttributeError):row['unknown'].append('WSL absence query failed')
    else:
        text=q.stdout.decode('utf-16-le').replace('\x00','').strip().lstrip('\ufeff')
        for distro in [s.strip() for s in text.splitlines() if s.strip()]:
            p=subprocess.run(['wsl.exe','-d',distro,'--exec','ps','-eo','pid,lstart,pcpu,rss,args'],capture_output=True)
            output=p.stdout.decode('utf-8',errors='replace')
            row['wsl'].append({'distro':distro,'exit':p.returncode,'stdout':output})
            if p.returncode:row['unknown'].append('WSL process query failed: '+distro)
            else:
                for line in output.splitlines()[1:]:
                    if re.search(r'(?:^|[ /])(vivado|xelab|xsimk?|xvlog|xvhdl|verilator(?:_bin)?|yosys|nextpnr|vvp|cc1plus)(?:$|[ .-])',line,re.I):
                        row['heavy'].append({'distro':distro,'process_line':line})
    row['query_known']=not row['unknown']
    row['other_heavy']=len(row['heavy'])
    row['process_inventory_hash']=hashlib.sha256(json.dumps({'heavy':row['heavy'],'wsl':row['wsl']},sort_keys=True).encode()).hexdigest()
    return assess_resources(row)


if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--stage',required=True);ap.add_argument('--output',required=True)
    args=ap.parse_args();receipt=resource_gate(Path(__file__).resolve().parents[1],args.stage)
    Path(args.output).write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:receipt[k] for k in ['status','host','physical_available_gib','commit_headroom_gib','other_heavy','query_known','unknown']}))
    raise SystemExit({'READY':0,'REFUSED':2,'UNKNOWN':3}[receipt['status']])
