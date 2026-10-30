"""Recover only audited offline artifacts; never deploy to a device."""
import hashlib, json, pathlib, tarfile

root = pathlib.Path(__file__).resolve().parents[1]
package = root / 'evidence/20261009-takeover/offline-target-evidence.tar.gz'
expected = 'bcd2a210014409516eeeb8e5cc1963b2c96ed9ac0e1fa7b6a7e0cb986f53a4ce'
assert hashlib.sha256(package.read_bytes()).hexdigest() == expected
with tarfile.open(package) as archive:
    for member in archive.getmembers():
        path = pathlib.PurePosixPath(member.name)
        assert not path.is_absolute() and '..' not in path.parts
        assert member.isdir() or member.isfile(), member.name
        target = root.joinpath(*path.parts)
        assert target.resolve().is_relative_to(root.resolve())
        if member.isfile():
            data = archive.extractfile(member).read()
            if target == root / 'toolchain.lock.json' and target.exists():
                assert json.loads(target.read_text()) == json.loads(data)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
audit = json.loads((root / 'evidence/target-audit.json').read_text())
for artifact in audit['artifacts']:
    data = (root / artifact['path']).read_bytes()
    assert len(data) == artifact['bytes']
    assert hashlib.sha256(data).hexdigest() == artifact['sha256']
assert (root / 'build/host-layout.bin').read_bytes() == (root / 'build/target-layout.bin').read_bytes()
result = {'scope': 'OFFLINE_PACKAGE_ONLY', 'target_package_sha256': expected,
          'artifacts_verified': len(audit['artifacts']), 'abi': 'IDENTICAL',
          'board': 'NOT_RUN', 'sd': 'NOT_TOUCHED'}
(root / 'evidence/20261009-takeover/target-package-verification.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
