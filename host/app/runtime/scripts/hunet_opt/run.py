#!/usr/bin/env python3
"""Capture one command without retries or overwriting an existing run."""
import argparse
from datetime import datetime, timezone
import getpass
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def source_identity(cwd):
    git = shutil.which('git')
    if not git:
        return {'available': False, 'reason': 'git unavailable'}
    def read(*args):
        return subprocess.check_output([git, '-C', str(cwd), *args], stderr=subprocess.DEVNULL)
    try:
        root = Path(os.fsdecode(read('rev-parse', '--show-toplevel').strip()))
        head = read('rev-parse', 'HEAD').decode('ascii').strip()
        entries = []
        for raw in read('ls-files', '-z').split(b'\0'):
            if not raw:
                continue
            name = os.fsdecode(raw)
            path = root / name
            entries.append({'path': name, 'sha256': sha256(path) if path.is_file() else None})
        canonical = json.dumps(entries, ensure_ascii=True, sort_keys=True,
                               separators=(',', ':')).encode('utf-8')
        return {'available': True, 'root': str(root), 'head': head,
                'tracked_sha256': hashlib.sha256(canonical).hexdigest(),
                'tracked_status': os.fsdecode(read('status', '--porcelain', '--untracked-files=no'))}
    except (OSError, subprocess.CalledProcessError) as exc:
        return {'available': False, 'reason': type(exc).__name__}


def run(argv: list, cwd: Path, out: Path) -> int:
    cwd, out = Path(cwd).resolve(), Path(out).resolve()
    if not argv:
        raise ValueError('empty argv')
    out.mkdir(parents=True, exist_ok=False)
    receipt = {'schema': 1, 'argv': list(argv), 'cwd': str(cwd),
               'host': platform.node(), 'account': getpass.getuser(),
               'utc_start': datetime.now(timezone.utc).isoformat(),
               'source': source_identity(cwd), 'input_files': [],
               'child_started': False}
    for arg in argv:
        path = Path(arg)
        if not path.is_absolute():
            path = cwd / path
        if path.is_file():
            receipt['input_files'].append({'path': str(path), 'sha256': sha256(path)})
    start = time.monotonic_ns()
    with (out / 'stdout.log').open('xb') as stdout, (out / 'stderr.log').open('xb') as stderr:
        try:
            process = subprocess.Popen(argv, cwd=str(cwd), stdout=stdout, stderr=stderr)
            receipt['child_started'] = True
            code = process.wait()
        except OSError as exc:
            code = 127
            receipt['launch_error'] = str(exc)
            stderr.write((str(exc) + '\n').encode('utf-8'))
    receipt.update(exit_code=code, elapsed_ns=time.monotonic_ns() - start,
                   utc_end=datetime.now(timezone.utc).isoformat())
    with (out / 'command.json').open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--task', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cwd', type=Path, default=Path.cwd())
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('command required after --')
    return run(command, args.cwd, args.out)


if __name__ == '__main__':
    raise SystemExit(main())
