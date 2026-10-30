"""Verify every archived member before an approved exclusive-cache release."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
from scripts.nnrt.verify_candidate import file_sha256


def safe_name(name):
    if not isinstance(name,str) or not name or '\\' in name or ':' in name:
        raise ValueError('UNSAFE_MEMBER')
    path=PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or path.as_posix()!=name or name=='.':
        raise ValueError('UNSAFE_MEMBER')
    return path


def manifest_rows(manifest):
    if not isinstance(manifest,dict) or not isinstance(manifest.get('source_root'),str):
        raise ValueError('SOURCE_ROOT_REQUIRED')
    rows=manifest.get('files')
    if not isinstance(rows,list) or not rows:raise ValueError('EMPTY_MANIFEST')
    result={}
    for row in rows:
        safe_name(row['path'])
        if row['path'] in result or type(row['bytes']) is not int or row['bytes']<0:
            raise ValueError('INVALID_OR_DUPLICATE_MEMBER')
        digest=row.get('sha256')
        if not isinstance(digest,str) or len(digest)!=64 or any(x not in '0123456789abcdef' for x in digest):
            raise ValueError('INVALID_SHA256')
        result[row['path']]=row
    return result


def verify_archive(archive, manifest):
    archive=Path(archive)
    if archive.is_symlink() or not archive.is_file():raise ValueError('REGULAR_ARCHIVE_REQUIRED')
    expected=manifest_rows(manifest);seen=set()
    initial=archive.stat()
    try:
        with tarfile.open(str(archive),'r:*') as tf:
            for member in tf:
                safe_name(member.name)
                if member.name in seen or not member.isfile() or member.name not in expected:
                    raise ValueError('UNEXPECTED_DUPLICATE_OR_SPECIAL_MEMBER')
                seen.add(member.name);row=expected[member.name]
                if member.size!=row['bytes']:raise ValueError('ARCHIVE_SIZE_MISMATCH')
                h=hashlib.sha256();size=0
                with tf.extractfile(member) as stream:
                    for block in iter(lambda:stream.read(1024*1024),b''):h.update(block);size+=len(block)
                if size!=row['bytes'] or h.hexdigest()!=row['sha256']:raise ValueError('ARCHIVE_CONTENT_MISMATCH')
        if seen!=set(expected):raise ValueError('MISSING_MEMBER')
        sha=file_sha256(archive)
        after=archive.stat()
        if (initial.st_size,initial.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):raise ValueError('ARCHIVE_CHANGED')
    except (tarfile.TarError,OSError,EOFError) as error:
        raise ValueError('ARCHIVE_UNREADABLE: '+str(error))
    return {'verified':True,'archive':str(archive.resolve()),'archive_bytes':after.st_size,'archive_sha256':sha,
            'manifest':json.loads(json.dumps(manifest)),'member_count':len(seen)}
