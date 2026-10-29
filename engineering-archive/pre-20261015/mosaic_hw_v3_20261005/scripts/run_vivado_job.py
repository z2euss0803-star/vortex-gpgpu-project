"""Run Vivado with file-backed output and a bounded monitoring interval."""
from pathlib import Path
import datetime
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def run(script, logfile, journal):
    source = (ROOT / 'scripts' / script).resolve()
    log = Path(logfile).resolve()
    jou = Path(journal).resolve()
    if source.parent != ROOT / 'scripts' or not source.is_file():
        raise ValueError('Unknown task script')
    if log.parent != ROOT / 'evidence' or jou.parent != ROOT / 'evidence':
        raise ValueError('Logs must stay in this candidate evidence directory')
    command = [r'G:\xilinx\Vivado\2020.1\bin\vivado.bat', '-mode', 'batch',
               '-source', str(source), '-log', str(log), '-journal', str(jou)]
    env = os.environ.copy()
    env['PROCESSOR_ARCHITECTURE'] = 'AMD64'
    stamp = lambda: datetime.datetime.now().astimezone().isoformat()
    with Path(str(log) + '.console.txt').open('wb') as output:
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
        state.update(status='exited', exit_code=code, finished=stamp())
        state_path.write_text(json.dumps(state, indent=2), encoding='utf-8')
    return code

if __name__ == '__main__':
    sys.exit(run(*sys.argv[1:]))
