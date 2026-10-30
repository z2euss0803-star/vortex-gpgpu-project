"""Release only approved exact regular files after fresh archive/source checks."""
import contextlib
import os
from pathlib import Path
from scripts.nnrt.archive_manifest import manifest_rows, verify_archive
from scripts.nnrt.verify_candidate import file_sha256


def snapshot(source):
    rows={}
    for folder,dirs,files in os.walk(str(source),followlinks=False):
        for name in dirs+files:
            p=Path(folder)/name;st=p.lstat()
            if p.is_symlink() or getattr(st,'st_file_attributes',0)&0x400:raise ValueError('LINK_REFUSED')
        for name in files:
            p=Path(folder)/name
            if not p.is_file() or p.stat().st_nlink!=1:raise ValueError('SPECIAL_OR_SHARED_INODE_REFUSED')
            rows[p.relative_to(source).as_posix()]={'bytes':p.stat().st_size,'sha256':file_sha256(p)}
    return rows


class ReleaseError(ValueError):
    def __init__(self,message,report):
        super().__init__(message);self.report=report


def release_verified_cache(source, receipt, allowlist, dry_run=True):
    source=Path(source).absolute()
    if source.is_symlink() or source!=source.resolve() or not source.is_dir():raise ValueError('EXACT_REGULAR_SOURCE_REQUIRED')
    if allowlist.get('approved') is not True or allowlist.get('exclusive_rebuildable') is not True:
        raise ValueError('EXPLICIT_EXCLUSIVE_APPROVAL_REQUIRED')
    if allowlist.get('active') is not False or allowlist.get('dependencies')!=[]:raise ValueError('ACTIVE_OR_SHARED_DEPENDENCY')
    if allowlist.get('source_root')!=str(source) or receipt.get('verified') is not True:raise ValueError('SOURCE_OR_RECEIPT_MISMATCH')
    manifest=receipt['manifest']
    if manifest['source_root']!=str(source):raise ValueError('ARCHIVE_SOURCE_MISMATCH')
    archive=Path(receipt['archive'])
    try:archive.resolve().relative_to(source)
    except ValueError:pass
    else:raise ValueError('ARCHIVE_MUST_BE_OUTSIDE_RELEASE_SOURCE')
    rows=manifest_rows(manifest)
    if sorted(allowlist.get('paths',[]))!=sorted(rows):raise ValueError('EXACT_ALLOWLIST_REQUIRED')
    with contextlib.ExitStack() as locks:
        for name in allowlist.get('lock_files',[]):
            try:
                import fcntl
                stream=locks.enter_context(Path(name).open('rb'))
                fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except (ImportError,OSError):raise ValueError('ACTIVE_OR_UNAVAILABLE_LOCK')
        fresh=verify_archive(archive,manifest)
        if fresh!=receipt:raise ValueError('ARCHIVE_RECEIPT_DRIFT')
        actual=snapshot(source)
        expected={n:{'bytes':r['bytes'],'sha256':r['sha256']} for n,r in rows.items()}
        if actual!=expected:raise ValueError('SOURCE_CHANGED_OR_UNLISTED_FILES')
        report={'dry_run':dry_run,'source_root':str(source),'archive':fresh,'released':[],
                'planned':sorted(rows),'logical_bytes':sum(r['bytes'] for r in rows.values())}
        if dry_run:return report
        try:
            for name in sorted(rows):
                p=source/name
                # No following a replaced path, no wildcard or directory deletion.
                if p.is_symlink() or p.resolve()!=p or not p.is_file() or p.stat().st_nlink!=1:
                    raise ValueError('SOURCE_CHANGED_DURING_RELEASE')
                p.resolve().relative_to(source)
                if p.stat().st_size!=rows[name]['bytes'] or file_sha256(p)!=rows[name]['sha256']:
                    raise ValueError('SOURCE_CHANGED_DURING_RELEASE')
                p.unlink();report['released'].append(name)
        except (OSError,ValueError) as error:
            raise ReleaseError('PARTIAL_RELEASE_STOPPED: '+str(error),report)
        return report
