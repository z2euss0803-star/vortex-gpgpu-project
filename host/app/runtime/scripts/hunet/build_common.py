"""Confined extraction, captured commands and compiled source-binding checks."""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import tarfile
from package_release import file_bytes, safe_name, verify_release


def sha(path): return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    pathlib.Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')


def checked_release(path):
    path = pathlib.Path(path).resolve()
    result = verify_release(path)
    if result['integrity'] != 'PASS': raise ValueError(result)
    return json.loads(file_bytes(path, 'release.json'))


def source_snapshot(release, destination):
    destination = pathlib.Path(destination)
    destination.mkdir(exist_ok=False)
    with tarfile.open(pathlib.Path(release)/'source.tar.gz', 'r:gz') as archive:
        for item in archive:
            name = safe_name(item.name)
            if not item.isfile(): raise ValueError('SOURCE_NONREGULAR')
            target = destination/name
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream: stream.write(archive.extractfile(item).read())
    (destination/'build').mkdir(exist_ok=True)
    header = file_bytes(pathlib.Path(release), 'build/vxl_hunet_source.h')
    for target in (destination/'build/vxl_hunet_source.h', destination/'driver/vxl_hunet_source.h'):
        target.write_bytes(header)
    return destination


def native_output(path):
    path = pathlib.Path(path).resolve()
    if path.exists(): raise ValueError('FRESH_OUTPUT_REQUIRED')
    path.parent.mkdir(parents=True, exist_ok=True)
    kind = subprocess.check_output(['stat', '-f', '-c', '%T', str(path.parent)], universal_newlines=True).strip()
    if kind not in ('ext2/ext3', 'ext4', 'xfs', 'btrfs'): raise ValueError('NATIVE_FILESYSTEM_REQUIRED:'+kind)
    path.mkdir()
    return path


def captured(command, log, cwd=None):
    with pathlib.Path(log).open('xb') as output:
        result = subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT)
    pathlib.Path(str(log)+'.exit').write_text(str(result.returncode)+'\n')
    if result.returncode: raise ValueError('COMMAND_EXIT:'+str(result.returncode)+':'+str(log))


def readelf(tool, path, *options):
    return subprocess.check_output([tool, *options, str(path)], universal_newlines=True)


def elf_record(tool, path, release, bound=True):
    path = pathlib.Path(path)
    header = readelf(tool, path, '-hW')
    program = readelf(tool, path, '-lW')
    dynamic = readelf(tool, path, '-dW')
    versions = readelf(tool, path, '-VW')
    machine = re.search(r'Machine:\s*([^\n]+)', header)
    loader = re.search(r'Requesting program interpreter:\s*([^\]]+)', program)
    data = path.read_bytes()
    return dict(machine=machine.group(1).strip() if machine else None,
        interpreter=loader.group(1).strip() if loader else None,
        needed=re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic),
        required_versions=sorted(set(re.findall(r'Name: ((?:GLIBC|GLIBCXX|CXXABI|GCC)_[\w.]+)', versions))),
        sha256=sha(path), bytes=len(data),
        source_commit=release['source_commit'] if bound and release['source_commit'].encode() in data else None,
        source_digest=release['source_digest'] if bound and bytes.fromhex(release['source_digest']) in data else None)


def compiler_identity(prefix):
    gcc, gxx = prefix+'gcc', prefix+'g++'
    target = subprocess.check_output([gcc, '-dumpmachine'], universal_newlines=True).strip()
    version = subprocess.check_output([gcc, '--version'], universal_newlines=True).splitlines()[0]
    return dict(target=target, version=version, gcc_sha256=sha(gcc), gxx_sha256=sha(gxx),
                gcc_path=str(pathlib.Path(gcc).resolve()), gxx_path=str(pathlib.Path(gxx).resolve()),
                account=os.environ.get('USER'), hostname=os.uname().nodename)
