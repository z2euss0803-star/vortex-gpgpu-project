"""Preserve a process past a monitoring deadline, record its natural exit.

The exit belongs to the exact Popen command. A batch-launcher exit is not
silently promoted to a separately observed grandchild/native-tool exit.
"""
import datetime
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path
import psutil


class WindowsChildren:
    """Hold real descendant handles before exit; never terminate a process."""
    def __init__(self, parent_pid):
        self.parent_pid, self.rows, self.handles, self.unknown = parent_pid, {}, {}, []
        self.api = None
        if os.name == 'nt':
            import ctypes
            from ctypes import wintypes
            self.ctypes = ctypes
            self.api = ctypes.WinDLL('kernel32', use_last_error=True)
            self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.api.OpenProcess.restype = wintypes.HANDLE
            self.api.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
            self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE,wintypes.DWORD]
            self.api.CloseHandle.argtypes = [wintypes.HANDLE]

    def sample(self, discover=True):
        if self.api is None:
            return False
        if discover:
            try:
                children = psutil.Process(self.parent_pid).children(recursive=True)
                for child in children:
                    if child.pid in self.rows:
                        continue
                    handle = None
                    try:
                        # Acquire the exit handle before reading the PEB-backed
                        # command/cwd. A short-lived child can otherwise vanish
                        # during those reads; failures remain unknown/refused.
                        handle = self.api.OpenProcess(0x100000 | 0x1000,False,child.pid)
                        if not handle:
                            raise OSError(self.ctypes.get_last_error(),'OpenProcess')
                        created = child.create_time()
                        row = {'pid':child.pid,'command':child.cmdline(),'cwd':child.cwd(),
                               'created_at_utc':datetime.datetime.fromtimestamp(created,datetime.timezone.utc).isoformat(),
                               'exit_code':None,'observed_exit_at_utc':None}
                        if child.create_time() != created:
                            raise RuntimeError('PID creation identity changed')
                        self.rows[child.pid], self.handles[child.pid] = row, handle
                    except (psutil.Error,OSError,RuntimeError) as error:
                        if handle:self.api.CloseHandle(handle)
                        item={'pid':child.pid,'error':str(error)}
                        if item not in self.unknown:self.unknown.append(item)
            except psutil.NoSuchProcess:
                pass
            except psutil.Error as error:
                item={'pid':self.parent_pid,'error':str(error)}
                if item not in self.unknown:self.unknown.append(item)
        alive = False
        for pid, handle in self.handles.items():
            if self.rows[pid]['exit_code'] is not None:
                continue
            state=self.api.WaitForSingleObject(handle,0)
            if state == 0:
                code=self.ctypes.c_ulong()
                if self.api.GetExitCodeProcess(handle,self.ctypes.byref(code)):
                    self.rows[pid].update(exit_code=code.value,observed_exit_at_utc=utc_now())
                else:
                    raise OSError(self.ctypes.get_last_error(),'GetExitCodeProcess')
            elif state == 258:
                alive=True
            else:
                raise OSError(self.ctypes.get_last_error(),'WaitForSingleObject')
        return alive

    def close(self):
        for handle in self.handles.values():self.api.CloseHandle(handle)


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def require_native_exit(receipt, required_names):
    if receipt['exit_code'] != 0:
        raise ValueError('nonzero launcher exit')
    if receipt.get('descendant_identity_unknown'):
        raise ValueError('descendant identity is unknown')
    for name in required_names:
        roots=[receipt['native_root_process']] if receipt.get('native_root_process') else []
        rows = [row for row in receipt.get('children', [])+roots
                if row.get('command') and Path(row['command'][0]).name.lower()==name.lower()]
        if not rows:
            raise ValueError('missing natural native exit: '+name)
        for row in rows:
            if not row.get('created_at_utc') or not row.get('cwd') or not row.get('pid'):
                raise ValueError('native process identity is incomplete: '+name)
            if row.get('exit_code') is None:
                raise ValueError('missing natural native exit: '+name)
            if row['exit_code'] != 0:
                raise ValueError('nonzero native exit: '+name)


def run_preserving(command, *, cwd, log_path, events_path, env=None,
                   deadline_seconds=7200, poll_seconds=1):
    if deadline_seconds <= 0 or poll_seconds <= 0:
        raise ValueError('positive monitoring deadline and poll interval required')
    cwd = Path(cwd).resolve()
    log_path, events_path = Path(log_path), Path(events_path)
    command = [str(item) for item in command]
    # Exclusive creation protects prior attempts. No timeout/kill/context-manager
    # cleanup can terminate the launched process at the monitoring deadline.
    with log_path.open('x', encoding='utf8') as log, events_path.open('x', encoding='utf8') as events:
        def record(event, **data):
            row = {'event': event, 'sample_at_utc': utc_now(), **data}
            events.write(json.dumps(row, ensure_ascii=False)+'\n')
            events.flush()
            return row
        started = time.monotonic()
        process = subprocess.Popen(command, cwd=cwd, env=env,
                                   stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            created = datetime.datetime.fromtimestamp(
                psutil.Process(process.pid).create_time(), datetime.timezone.utc).isoformat()
        except psutil.Error:
            created = None
        record('started', pid=process.pid, created_at_utc=created,
               command=command, cwd=str(cwd), deadline_seconds=deadline_seconds,
               deadline_policy='record only; preserve process and wait for natural exit')
        exceeded = False
        descendants = WindowsChildren(process.pid)
        while True:
            parent_running = process.poll() is None
            child_running = descendants.sample(discover=parent_running)
            if not parent_running and not child_running:
                break
            elapsed = time.monotonic()-started
            if not exceeded and elapsed >= deadline_seconds:
                exceeded = True
                record('deadline_exceeded', pid=process.pid, elapsed_seconds=elapsed,
                       action='preserved; no termination')
            time.sleep(poll_seconds)
        result = record('exited', pid=process.pid, exit_code=process.returncode,
                        exit_code_scope='exact launched command; batch/native child distinction retained',
                        elapsed_seconds=time.monotonic()-started,
                        deadline_exceeded=exceeded,children=list(descendants.rows.values()),
                        descendant_identity_unknown=descendants.unknown,
                        descendant_capture_scope='observed Windows descendants; missed/short-lived children are not inferred')
        descendants.close()
    result.update(command=command, cwd=str(cwd), created_at_utc=created)
    result['log_sha256'] = hashlib.sha256(log_path.read_bytes()).hexdigest()
    return result


def native_exit_verdict(receipt, native_names):
    from pathlib import PureWindowsPath
    names={n.lower() for n in native_names}
    root=dict(receipt)
    if not root.get('command'): root['command']=[]
    rows=[root]+receipt.get('children',[])
    matched=[]
    for r in rows:
        command=r.get('command') or []
        if command and PureWindowsPath(str(command[0])).name.lower() in names:matched.append(r)
    reasons=[]
    if receipt.get('descendant_identity_unknown'):reasons.append('descendant identity unknown')
    if not matched:reasons.append('missing native tool identity')
    for r in matched:
        if not r.get('pid') or not (r.get('created_at_utc') or r.get('created_at')) or not r.get('cwd') or r.get('exit_code') is None:
            reasons.append('incomplete native identity or natural exit')
    if reasons:return {'status':'UNKNOWN','reasons':reasons}
    if any(r['exit_code']!=0 for r in matched) or receipt.get('exit_code')!=0:
        return {'status':'FAIL','reasons':['nonzero natural exit']}
    return {'status':'PASS','reasons':[]}
