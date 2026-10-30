"""Content admission for an explicitly frozen candidate (Python 3.6+)."""
import hashlib
from pathlib import Path


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def verify_candidate(root, frozen):
    root = Path(root).resolve(strict=True)
    errors, checked, seen = [], [], set()
    entries = frozen.get('files', []) if isinstance(frozen, dict) else []
    if not isinstance(entries, list) or not entries:
        return {'admitted': False, 'errors': ['EMPTY_OR_INVALID_MANIFEST'], 'checked': []}
    for entry in entries:
        try:
            name = entry['path']
            rel = Path(name)
            if not isinstance(name, str) or not name or rel.is_absolute() or '..' in rel.parts or '\\' in name or ':' in name:
                raise ValueError('UNSAFE_PATH')
            if name in seen:
                raise ValueError('DUPLICATE_PATH')
            seen.add(name)
            path = root / rel
            current = root
            for part in rel.parts:
                current = current / part
                if current.is_symlink():
                    raise ValueError('SYMLINK_REFUSED')
            path.resolve(strict=True).relative_to(root)
            if not path.is_file():
                raise ValueError('NOT_REGULAR_FILE')
            actual = {'path': name, 'bytes': path.stat().st_size, 'sha256': file_sha256(path)}
            checked.append(actual)
            if actual['bytes'] != entry['bytes'] or actual['sha256'] != entry['sha256']:
                raise ValueError('CONTENT_DRIFT')
        except (KeyError, TypeError, ValueError, OSError) as error:
            errors.append({'entry': entry, 'error': str(error)})
    return {'admitted': not errors, 'errors': errors, 'checked': checked}
