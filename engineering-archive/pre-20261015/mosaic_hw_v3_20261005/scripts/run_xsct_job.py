"""Run local XSCT builds with raw file output and explicit exit-code handling.

Windows PowerShell 5 treats native stderr as NativeCommandError when its
ErrorActionPreference is Stop. Archiver progress is valid stderr, so it must
not travel through that PowerShell error stream. Build PASS markers and
artifact audits remain separate requirements in the pipeline.
"""
from pathlib import Path
import datetime
import json
import os
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]


def run(script, logfile, *arguments):
    source = (ROOT / 'scripts' / script).resolve()
    log = (ROOT / 'evidence' / logfile).resolve()
    if source.parent != ROOT / 'scripts' or not source.is_file():
        raise ValueError('Unknown local XSCT script')
    if log.parent != ROOT / 'evidence':
        raise ValueError('Log must stay in this candidate evidence directory')
    token = uuid.uuid4().hex
    command = [r'G:\xilinx\Vitis\2020.1\bin\xsct.bat',
               str(ROOT / 'scripts' / 'run_xsct_guarded.tcl'), token,
               str(source), *arguments]
    env = os.environ.copy()
    env['PROCESSOR_ARCHITECTURE'] = 'AMD64'
    stamp = lambda: datetime.datetime.now().astimezone().isoformat()
    with log.open('wb') as output:
        process = subprocess.Popen(command, cwd=ROOT, env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
        state = {'launcher_pid': os.getpid(), 'process_pid': process.pid,
                 'command': command, 'started': stamp()}
        state_path = Path(str(log) + '.process.json')
        while True:
            state.update(status='running', checked=stamp())
            state_path.write_text(json.dumps(state, indent=2), encoding='utf-8')
            try:
                code = process.wait(timeout=180)
                break
            except subprocess.TimeoutExpired:
                pass
    raw = log.read_text(encoding='utf-8-sig', errors='replace')
    matches = re.findall(r'^XSCT_GUARDED_RESULT ' + token + r' (\d+)\s*$', raw, re.M)
    passed = code == 0 and matches == ['0']
    result_code = 0 if passed else (code or 1)
    state.update(status='exited', native_exit_code=code, exit_code=result_code,
                 guarded_results=matches, finished=stamp())
    state_path.write_text(json.dumps(state, indent=2), encoding='utf-8')
    return result_code


if __name__ == '__main__':
    sys.exit(run(*sys.argv[1:]))
