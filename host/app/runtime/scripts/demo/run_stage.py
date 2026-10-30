"""Fixed offline stage registry; no caller-supplied commands or field stages."""
import datetime
import json
from pathlib import Path
import subprocess
import sys
import time
from .identity import verify_sources

# Stages are registered only when their implementation and semantic checks exist.
STAGES = {}
FORBIDDEN = frozenset(('board', 'sd', 'field', 'reset', 'pl', 'uart'))


def run_stage(stage, env_path, out):
    if stage in FORBIDDEN: raise PermissionError('field stage not authorized')
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    begin = time.monotonic_ns() if hasattr(time, 'monotonic_ns') else int(time.monotonic()*1000000000)
    receipt = dict(stage=stage, scope='OFFLINE_NOT_BOARD', exit=1,
                   semantic='NOT_IMPLEMENTED', gpu_raw_cycles=None, argv=[],
                   utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    stdout = ''; stderr = ''
    try:
        env = json.loads(Path(env_path).read_text(encoding='utf-8'))
        if stage == 'identity':
            result = verify_sources(env['source_manifest'])
            receipt.update(result)
            stdout = json.dumps(result)+'\n'
        elif stage in STAGES:
            argv, marker = STAGES[stage]
            receipt['argv'] = [sys.executable if word == '{python}' else word for word in argv]
            process = subprocess.Popen(receipt['argv'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            raw_out, raw_err = process.communicate()
            (out/'stdout.bin').write_bytes(raw_out); (out/'stderr.bin').write_bytes(raw_err)
            stdout = raw_out.decode('utf-8', 'replace'); stderr = raw_err.decode('utf-8', 'replace')
            receipt['exit'] = process.returncode
            receipt['semantic'] = marker if process.returncode == 0 and marker in stdout else 'FAIL_OR_MISSING_SEMANTIC'
            if receipt['semantic'] == 'FAIL_OR_MISSING_SEMANTIC' and process.returncode == 0:
                receipt['semantic_exit'] = 1
        else:
            stderr = 'stage is not implemented\n'
    except Exception as error:
        receipt['semantic'] = 'FAILED'
        stderr = str(error)+'\n'
    end = time.monotonic_ns() if hasattr(time, 'monotonic_ns') else int(time.monotonic()*1000000000)
    receipt['clock_domain'] = 'HOST_MONOTONIC'
    receipt['elapsed_ns'] = end-begin
    (out/'stdout.txt').write_text(stdout, encoding='utf-8')
    (out/'stderr.txt').write_text(stderr, encoding='utf-8')
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', required=True); parser.add_argument('--env', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    result = run_stage(args.stage, args.env, args.out)
    print(json.dumps(result)); raise SystemExit(result.get('semantic_exit', result['exit']))
