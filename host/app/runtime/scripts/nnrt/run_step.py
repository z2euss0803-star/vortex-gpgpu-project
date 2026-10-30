"""Execute an offline argv, retaining natural exit and independent semantics."""
import argparse
import datetime
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import time

from scripts.nnrt.verify_candidate import file_sha256, verify_candidate


ENV_KEYS = ('PATH', 'HOME', 'USER', 'LOGNAME', 'SHELL', 'SYSTEMROOT', 'SystemRoot',
            'WINDIR', 'COMSPEC', 'TEMP', 'TMP', 'TMPDIR', 'LANG', 'LC_ALL',
            'PYTHONPATH', 'PYTHONIOENCODING', 'PYTHONDONTWRITEBYTECODE',
            'LD_LIBRARY_PATH', 'PKG_CONFIG_PATH', 'SOURCE_DATE_EPOCH',
            'CROSS_COMPILE', 'CC', 'CXX', 'AR', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS')


def _json(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, ensure_ascii=False, sort_keys=True, indent=2)
        stream.write('\n')


def run_step(argv, cwd, out, identity):
    if not isinstance(argv, list) or not argv or not all(isinstance(x, str) and '\x00' not in x for x in argv):
        raise ValueError('NONEMPTY_ARGV_ARRAY_REQUIRED')
    if identity.get('scope') not in ('HOST_PROTOCOL_TEST', 'OFFLINE_VALIDATION', 'CROSS_COMPILE', 'IMAGE_BUILD'):
        raise ValueError('OFFLINE_SCOPE_REQUIRED')
    if identity.get('gpu_raw_cycles') is not None:
        raise ValueError('OFFLINE_GPU_CYCLES_MUST_BE_NULL')
    cwd, out = Path(cwd).resolve(strict=True), Path(out).absolute()
    if out.exists() or out.is_symlink():
        raise FileExistsError(str(out))
    source_manifest = identity.get('source_manifest', {})
    source = verify_candidate(cwd, source_manifest)
    if not source['admitted']:
        raise ValueError('SOURCE_ADMISSION_FAILED: ' + json.dumps(source['errors']))
    out.mkdir(parents=True, exist_ok=False)
    env = {key: os.environ[key] for key in ENV_KEYS if key in os.environ}
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    start = time.monotonic()
    utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record = dict(identity)
    record.update(run_id=out.name, host=socket.gethostname(), account=os.environ.get('USERNAME', os.environ.get('USER')),
                  platform=platform.platform(), cwd=str(cwd), argv=argv, environment=env, utc=utc,
                  shell=False, gpu_raw_cycles=None)
    _json(out / 'identity.json', record)
    _json(out / 'source_manifest.json', source_manifest)
    _json(out / 'source_before.json', source)
    _json(out / 'command.txt', argv)
    rc, spawn_error = None, None
    try:
        with (out / 'stdout.log').open('xb') as stdout, (out / 'stderr.log').open('xb') as stderr:
            rc = subprocess.call(argv, cwd=str(cwd), env=env, stdout=stdout, stderr=stderr, shell=False)
    except OSError as error:
        spawn_error = repr(error)
    elapsed = time.monotonic() - start
    after = verify_candidate(cwd, source_manifest)
    semantic = 'PASS' if rc == 0 and after['admitted'] else 'FAILED'
    _json(out / 'source_after.json', after)
    _json(out / 'exit.json', {'natural_exit': rc, 'semantic_status': semantic, 'spawn_error': spawn_error})
    _json(out / 'metrics.json', {'clock_domain': 'HOST_MONOTONIC', 'host_elapsed_seconds': elapsed,
                               'gpu_raw_cycles': None, 'board_validated': False})
    with (out / 'events.jsonl').open('x', encoding='utf-8', newline='\n') as events:
        events.write(json.dumps({'event': 'PROCESS_FINISHED', 'natural_exit': rc, 'semantic_status': semantic,
                                'parent_failure_run_id': identity.get('parent_failure_run_id'), 'gpu_raw_cycles': None}) + '\n')
    if semantic != 'PASS':
        _json(out / 'failure.json', {'category': 'ORCHESTRATION' if spawn_error else 'COMMAND_OR_SOURCE_VALIDATION',
                                   'natural_exit': rc, 'source_admitted_after': after['admitted'],
                                   'parent_failure_run_id': identity.get('parent_failure_run_id')})
    _json(out / 'result_manifest.json', {'files': [
        {'path': p.name, 'bytes': p.stat().st_size, 'sha256': file_sha256(p)}
        for p in sorted(out.iterdir()) if p.is_file()]})
    if spawn_error:
        raise OSError(spawn_error)
    return rc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--cwd', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('argv', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    argv = args.argv[1:] if args.argv[:1] == ['--'] else args.argv
    identity = json.loads(Path(args.identity).read_text(encoding='utf-8'))
    return run_step(argv, Path(args.cwd), Path(args.out), identity)


if __name__ == '__main__':
    raise SystemExit(main())
