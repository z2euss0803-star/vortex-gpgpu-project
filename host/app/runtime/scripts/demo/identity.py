"""Admission of immutable inputs. Identity is never runtime evidence."""
import hashlib
import json
from pathlib import Path, PurePosixPath


def sha256_file(path):
    digest = hashlib.sha256()
    with open(str(path), 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def verify_sources(manifest_path):
    manifest_path = Path(manifest_path).resolve()
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('version') != 1 or not manifest.get('files'):
        raise ValueError('invalid source manifest')
    programs = manifest.get('programs', [])
    if programs != [{'id': 12, 'app': 'mosaic'}]:
        raise ValueError('program collision or unapproved program identity')
    roots = manifest.get('roots', {'default':str(manifest_path.parent)})
    seen = set()
    verified = []
    for row in manifest['files']:
        name = row['path']
        parts = PurePosixPath(name).parts
        root_name = row.get('root', 'default')
        key = (root_name, name)
        if (not name or '\\' in name or ':' in name or '\0' in name or
                name.startswith('/') or any(x in ('.', '..') for x in name.split('/')) or key in seen):
            raise ValueError('unsafe or duplicate source path')
        if row.get('role') not in ('production', 'build-input', 'evidence', 'tooling'):
            raise ValueError('unknown source role')
        seen.add(key)
        if root_name not in roots: raise ValueError('unknown source root')
        root = Path(roots[root_name]).resolve()
        target = root.joinpath(*parts)
        cursor = target
        while cursor != root:
            if cursor.is_symlink(): raise ValueError('source symlink rejected')
            cursor = cursor.parent
        if not target.is_file() or target.stat().st_size != row['bytes'] or sha256_file(target) != row['sha256']:
            raise ValueError('source identity mismatch: '+name)
        verified.append(dict(row))
    canonical = json.dumps(verified, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return dict(scope='SOURCE_ONLY_NOT_RUNTIME', exit=0,
                semantic='SOURCE_IDENTITY_MATCH_NOT_RUNTIME_PASS',
                source_sha=hashlib.sha256(canonical).hexdigest(),
                manifest_sha256=sha256_file(manifest_path), files=len(verified))


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=False)
    try:
        result = verify_sources(args.manifest)
    except Exception as error:
        result = dict(scope='SOURCE_ONLY_NOT_RUNTIME', exit=1, semantic='SOURCE_REJECTED', error=str(error))
    (out/'identity.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result)); return result['exit']


if __name__ == '__main__':
    raise SystemExit(main())
