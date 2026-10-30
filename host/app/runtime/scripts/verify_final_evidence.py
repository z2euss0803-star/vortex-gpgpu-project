"""Assert final compiled-source identity and bounded offline acceptance."""
import hashlib, json, pathlib

root = pathlib.Path(__file__).resolve().parents[1]
folder = root / 'evidence/20261009-takeover'
snapshot = json.loads((folder / 'source-manifest.json').read_text())
checked = []
for name, digest in snapshot.items():
    p = root / name
    if p.suffix in ('.c', '.cpp', '.h', '.sv', '.json', '.csv') and not name.startswith('scripts/') or p.name in ('Makefile', 'Kconfig'):
        assert hashlib.sha256(p.read_bytes()).hexdigest() == digest, name
        checked.append(name)
for path in ('fault-matrix-final.log.exit.json', 'cross-check-final.log.exit.json',
             'legacy-and-cross-final.log.exit.json', 'audit-final-frozen.log.exit.json'):
    assert json.loads((folder / path).read_text())['SSHExitCode'] == 0, path
results = json.loads((root / 'evidence/20261009-ubuntu-final/results.json').read_text())
assert len(results['gates']) == 8
assert all(x['exit'] == 0 and x['status'] == 'PASS' for x in results['gates'].values())
assert results['board'] == 'NOT_RUN'
state = json.loads((root / 'state.json').read_text())
assert state['offline'] == 'OFFLINE_READY'
assert state['h0_board'].startswith('NOT_RUN') and state['h1'].startswith('NOT_RUN')
out = {'scope': 'OFFLINE_EVIDENCE_ONLY', 'compiled_source_files_unchanged': len(checked),
       'host_gates': '8_OF_8_PASS', 'final_remote_commands': '4_OF_4_EXIT_ZERO',
       'h0_board': 'NOT_RUN', 'h1': 'NOT_RUN'}
(folder / 'final-evidence-verification.json').write_text(json.dumps(out, indent=2)+'\n')
print(json.dumps(out, indent=2))
