"""Actual matching-toolchain build; never execute a target ELF or access a board."""
import argparse
import json
import pathlib
import posixpath
import re
import tarfile
from build_common import captured, checked_release, compiler_identity, elf_record, native_output, readelf, sha, source_snapshot, write_json
from cross_audit import ARTIFACTS, audit_cross


def runtime_inventory(tarpath, needed, out, readelf_tool):
    with tarfile.open(tarpath, 'r:*') as archive:
        members = {}
        for item in archive:
            name = item.name
            while name.startswith('./'): name = name[2:]
            name = name.rstrip('/')
            if name.startswith('/') or '..' in name.split('/') or name in members: raise ValueError('ROOTFS_PATH_OR_DUPLICATE')
            members[name] = item
        def contents(name, seen=()):
            if name in seen or len(seen) > 16 or name not in members: raise ValueError('ROOTFS_LINK:'+name)
            item = members[name]
            if item.isfile(): return archive.extractfile(item).read()
            if item.issym() or item.islnk():
                target = item.linkname
                if item.issym() and not target.startswith('/'): target = posixpath.join(posixpath.dirname(name), target)
                target = posixpath.normpath(target.lstrip('/'))
                if target == '..' or target.startswith('../'): raise ValueError('ROOTFS_ESCAPE')
                return contents(target, seen+(name,))
            raise ValueError('ROOTFS_NONREGULAR:'+name)
        queue = list(needed)+['ld-linux-aarch64.so.1']; libraries = {}; versions = set()
        while queue:
            name = queue.pop()
            if name in libraries: continue
            path = next((p for p in ('lib/'+name, 'usr/lib/'+name) if p in members), None)
            if path is None: raise ValueError('MISSING_RUNTIME:'+name)
            data = contents(path); target = out/name; target.write_bytes(data)
            if 'Machine:                           AArch64' not in readelf(readelf_tool, target, '-hW'):
                # Parse spacing independently of the readelf version.
                if not re.search(r'Machine:\s+AArch64\s*\n', readelf(readelf_tool, target, '-hW')): raise ValueError('RUNTIME_ARCH:'+name)
            dynamic = readelf(readelf_tool, target, '-dW')
            queue += re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic)
            text = readelf(readelf_tool, target, '-VW')
            definitions = text.split('Version definition section', 1)[-1].split('Version needs section', 1)[0] if 'Version definition section' in text else ''
            versions.update(re.findall(r'Name: ((?:GLIBC|GLIBCXX|CXXABI|GCC)_[\w.]+)', definitions))
            libraries[name] = dict(path=path, sha256=sha(target))
    return libraries, sorted(versions)


def build(release_dir, out_dir, prefix, rootfs):
    if (pathlib.Path(release_dir)/'release-hybrid.json').is_file():
        import sys
        sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'hunet_hybrid'))
        from hybrid_cross import build_cross
        return build_cross(release_dir,out_dir,prefix,rootfs)
    manifest = checked_release(release_dir)
    output = native_output(out_dir)
    record = dict(scope='HUNET_AARCH64_CROSS', exit=None, source_commit=manifest['source_commit'],
        source_digest=manifest['source_digest'], elfs={}, rootfs_libraries=[], runtime_versions=[],
        rootfs_tar_sha256=sha(rootfs), board='NOT_RUN', sd='NOT_TOUCHED')
    try:
        record['compiler'] = compiler_identity(prefix)
        if record['compiler']['target'] != 'aarch64-xilinx-linux' or not re.search(r'\b9\.2\.0\b', record['compiler']['version']): raise ValueError('MATCHING_2020_1_COMPILER_REQUIRED')
        source = source_snapshot(release_dir, output/'source')
        command = ['make', '-C', str(source), 'userspace-cross', 'CROSS_COMPILE='+prefix,
            'SOURCE_COMMIT='+manifest['source_commit'], 'HUNET_BINDING_HEADER='+str(source/'build/vxl_hunet_source.h')]
        record['command'] = command
        captured(command, output/'build.log')
        record['exit'] = 0
        all_needed = set()
        for name in ARTIFACTS:
            value = elf_record(prefix+'readelf', source/'build/aarch64'/name, manifest)
            all_needed.update(value['needed']); record['elfs'][name] = value
        (output/'runtime').mkdir()
        libs, versions = runtime_inventory(rootfs, all_needed, output/'runtime', prefix+'readelf')
        record['rootfs_libraries'] = sorted(libs); record['runtime_versions'] = versions
        record['runtime_files'] = libs
    except Exception as error:
        record['error'] = str(error)
    result = audit_cross(record, manifest); record['audit'] = result
    write_json(output/'receipt.json', record)
    if result['integrity'] != 'PASS': raise ValueError(result)
    print('AARCH64_CROSS_PASS_NOT_BOARD', output, flush=True)
    return record


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--release', type=pathlib.Path, required=True);p.add_argument('--out', type=pathlib.Path, required=True)
    p.add_argument('--cross-compile', required=True);p.add_argument('--rootfs', type=pathlib.Path, required=True);a=p.parse_args()
    build(a.release,a.out,a.cross_compile,a.rootfs)
