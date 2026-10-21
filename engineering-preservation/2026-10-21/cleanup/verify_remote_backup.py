import hashlib
import io
import json
import pathlib
import subprocess
from datetime import datetime, timezone
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent
GIT = 'F:/git/Git/cmd/git.exe'
REPO = ROOT / 'git_readback.git'
PREFIX = 'engineering-preservation/2026-10-21/'
REF = 'refs/heads/verified-backup'

def git(*args):
    return subprocess.check_output([GIT, '-C', str(REPO), *args])

def blob(path):
    return git('cat-file', 'blob', REF + ':' + PREFIX + path)

def sha(b):
    return hashlib.sha256(b).hexdigest()

head = git('rev-parse', REF).decode().strip()
manifest_bytes = blob('PRESERVATION_MANIFEST.json')
manifest = json.loads(manifest_bytes)
checked = []
zip_checks = []
for entry in manifest['files']:
    b = blob(entry['backup'])
    assert len(b) == entry['bytes'], entry['backup']
    assert sha(b) == entry['sha256'], entry['backup']
    checked.append({'path': entry['backup'], 'bytes': len(b), 'sha256': sha(b)})
    if not entry['backup'].endswith('.zip'):
        continue
    with zipfile.ZipFile(io.BytesIO(b)) as z:
        assert z.testzip() is None
        if entry['backup'].endswith('verified_source_inputs_attempt8.zip'):
            run = ROOT.parent / 'mosaic_tcu_dual_s8_shared_b_20261018_v1/runs/20261020T215251688659Z_candidate_gpu_pair'
            members = json.loads((run / 'inputs_before.json').read_text(encoding='utf-8-sig'))['files']
            assert set(z.namelist()) == set(members)
            for name, expected in members.items():
                assert sha(z.read(name)) == expected, name
        else:
            mn = 'ELF_MEMBERS.json' if 'accepted_device_elfs' in entry['backup'] else 'PRESERVATION_MEMBERS.json'
            members = json.loads(z.read(mn))
            assert set(z.namelist()) == {x['path'] for x in members} | {mn}
            for m in members:
                data = z.read(m['path'])
                assert len(data) == m['bytes'] and sha(data) == m['sha256'], m['path']
        zip_checks.append({'path': entry['backup'], 'verified_members': len(members), 'all_member_sha256_pass': True})
receipt = {
    'utc': datetime.now(timezone.utc).isoformat(),
    'repository': 'https://github.com/z2euss0803-star/vortex-gpgpu-project',
    'visibility_verified': 'private',
    'branch': 'codex/f-drive-preservation-20261021',
    'commit': head,
    'verification': 'PASS_REMOTE_FETCH_EXACT_BYTES',
    'method': 'Independently fetched from GitHub into a fresh bare repository; git cat-file raw blob hashes compared with preservation manifest; all ZIP member hashes reread.',
    'manifest_sha256': sha(manifest_bytes),
    'file_count': len(checked),
    'files': checked,
    'zip_checks': zip_checks,
    'deleted': 0,
    'moved': 0,
    'scope': 'Selected preservation files only. Not a whole-drive backup or GPU functional/coverage acceptance.'
}
(ROOT / 'evidence/git_upload_receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:v for k,v in receipt.items() if k != 'files'}, ensure_ascii=False))
