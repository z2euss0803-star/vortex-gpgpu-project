"""A-only final HOST build, sealed model admission and immutable source lock.

No target ELF, GPU, network, board or SD execution. Run only after final merge.
"""
from pathlib import Path
import hashlib, json, os, resource, subprocess, sys, time, traceback

P = Path
G = P('/mnt/g/s8_software_r5_20261028')
S = G / 'software-r13-performance-633/source'
E = G / 'software-r13-performance-633/evidence/a-r13-models-host-668'
H = G / 'software-r13-performance-633/host-final-r13-668'
M = G / 'software-r13-performance-633/models-final-r13-668'
I = G / 'offline-model-inputs-545'
LOCK = S / 'build/source-lock-r13-668.json'
PRESET = 'af055aa4c09f41e57a4c4edb6a7ef106582350120c5b898251a3cbd4c09f9a12'
ALL = (2, 9, 19, 25, 32, 42, 52, 55, 62, 69, 76, 83, 90, 97, 100, 107)

def sha(p):
    h = hashlib.sha256()
    with P(p).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def wire(o):
    return (json.dumps(o, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode()

def durable(p, raw):
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open('xb') as f:
        assert f.write(raw) == len(raw)
        f.flush()
        os.fsync(f.fileno())
    assert p.read_bytes() == raw

def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    sys.dont_write_bytecode = True
    os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
    os.environ['TMPDIR'] = str(G / 'software-r13-performance-633/tmp-final-668')
    P(os.environ['TMPDIR']).mkdir(exist_ok=True)
    for p in (E, H, M):
        p.mkdir(parents=True, exist_ok=False)
    durable(E / 'script-used.py', P(__file__).read_bytes())
    sys.path.insert(0, str(S))
    from scripts.unified.cross_common import snapshot, verify_source_lock
    from scripts.unified.generate_hunet_package import generate as hunet
    from scripts.unified.generate_mosaic_package import generate as mosaic
    from scripts.unified.bind_kernel import firmware_inputs
    before = snapshot(S)
    durable(LOCK, wire(dict(schema=1, snapshot=before)))
    lock_sha = sha(LOCK)
    assert verify_source_lock(S, LOCK, lock_sha) == before
    rec = dict(scope='ACTUAL_R13_HOST_BUILD_AND_SEALED_MODEL_ADMISSION_NOT_GPU',
               natural_exit=1, target_executed=False, gpu_raw_cycles=None,
               board='NOT_ACCESSED', sd='NOT_ACCESSED', source_lock_sha256=lock_sha,
               source_before=before, source_after=None, commands=[], models=[])
    def save():
        (E / 'receipt.json').write_bytes(wire(rec))
    def run(tag, args, cwd=S):
        args = list(map(str, args))
        start = time.monotonic()
        print('BEGIN ' + tag, flush=True)
        so, se = E / (tag + '.stdout.log'), E / (tag + '.stderr.log')
        with so.open('xb') as a, se.open('xb') as b:
            q = subprocess.run(args, cwd=cwd, stdout=a, stderr=b)
        rec['commands'].append(dict(tag=tag, argv=args, cwd=str(cwd),
            natural_exit=q.returncode, elapsed_s=time.monotonic()-start,
            stdout=dict(path=str(so), sha256=sha(so)),
            stderr=dict(path=str(se), sha256=sha(se))))
        save()
        print('END ' + tag + ' ' + str(q.returncode), flush=True)
        if q.returncode:
            raise RuntimeError(tag + ': ' + se.read_text(errors='replace')[-4000:])
        return so.read_bytes()
    ram = P('/dev/shm/s8_final_models_668')
    packages = []
    try:
        jp = G / 'offline-rebuild-inputs-541/host-json-c/usr'
        base = ['make', '-f', 'Makefile.unified', 'OUT=' + str(H / 'out'),
                'DEBUG_FLAGS=-g1', 'CXX=g++ -Wno-dangling-reference',
                'NN_JSON_PREFIX=' + str(jp),
                'NN_JSON_LIBS=' + str(jp / 'lib/x86_64-linux-gnu/libjson-c.a') + ' -lm',
                'U18_DRM_PREFIX=' + str(G / 'readonly-host-drm-527/usr'), '-j4']
        rec['all_host_objects_built_fresh'] = True
        run('host-build', base + [H / 'out/vxgpud', H / 'out/image_package_metadata'])
        raw = (H / 'out/vxgpud').read_bytes()
        assert raw[:6] == b'\x7fELF\x02\x01' and int.from_bytes(raw[18:20], 'little') == 62
        for name, nodes in [('hunet', ()), ('hunet-gn9', (9,)), ('hunet-gn16', ALL)]:
            start = time.monotonic()
            hunet(I / 'hunet/weights-original.bin', M / name, 4, cpu_gn_stats_nodes=nodes)
            packages.append((name, 'hunet'))
            print('GENERATED ' + name + ' %.3fs' % (time.monotonic()-start), flush=True)
        for name, w, h, c, p, opt in [
            ('mosaic',1280,960,256,4096,0),
            ('mosaic-profile',1280,960,256,4096,1),
            ('mosaic-smoke',12,8,64,64,0),
            ('mosaic-smoke-profile',12,8,64,64,1),
            ('mosaic-b1024',1280,960,1024,4096,0),
            ('mosaic-b4096',1280,960,4096,4096,0),
        ]:
            start = time.monotonic()
            mosaic(I / 'mosaic/weights-original.ms8', I / 'mosaic/derived-weight-manifest.json',
                   I / ('mosaic/small-input.f32' if w == 12 else 'mosaic/full-input.f32'),
                   w, h, c, M / name, 4, pointwise_budget=p, profiling_options=opt,
                   variant='reuse-v8')
            packages.append((name, 'mosaic'))
            print('GENERATED ' + name + ' %.3fs' % (time.monotonic()-start), flush=True)
        assert not ram.exists()
        ram.mkdir(mode=0o700)
        inventory = {}
        for name, kind in packages:
            folder = M / name
            assert all(p.is_file() and not p.is_symlink() for p in folder.iterdir())
            pins = {p.name: dict(bytes=p.stat().st_size, sha256=sha(p)) for p in folder.iterdir()}
            assert pins
            inventory[name] = pins
            copy = ram / name
            copy.mkdir(mode=0o700)
            for leaf, pin in pins.items():
                f = copy / leaf
                f.write_bytes((folder / leaf).read_bytes())
                f.chmod(0o400)
                assert f.stat().st_uid == os.geteuid() and f.stat().st_mode & 0o777 == 0o400
                assert sha(f) == pin['sha256']
            copy.chmod(0o500)
            try:
                cmd = [H / 'out/image_package_metadata', kind, copy,
                       '/usr/share/vxgpu/models/' + name, name]
                if kind == 'hunet':
                    cmd += ['/usr/share/vxgpu/hunet-fixed-inputs', PRESET]
                admission = json.loads(run('admit-' + name, cmd))
                assert admission['installed_model']['id'] == name
                assert admission['gpu_raw_cycles'] is None
                assert admission['board_identity_verified'] is False
                assert admission['arena_pl_peak_bytes'] > 0
                rec['models'].append(dict(name=name, package=str(folder), admission=admission))
                save()
            finally:
                copy.chmod(0o700)
                for leaf, pin in pins.items():
                    f = copy / leaf
                    assert sha(f) == pin['sha256']
                    f.chmod(0o600)
                    f.unlink()
                copy.rmdir()
        ram.rmdir()
        assert not ram.exists()
        fw = firmware_inputs(S, 4, mosaic_variant='reuse-v8')
        durable(E / 'firmware-catalog.bin', fw['catalog'])
        durable(E / 'firmware-execution.bin', fw['execution'])
        fw_rows = [dict(entry_id=r['entry_id'], bytes=len(r['payload']),
                        sha256=hashlib.sha256(r['payload']).hexdigest()) for r in fw['entries']]
        assert len(fw_rows) == 16
        durable(E / 'model-file-inventory.json', wire(inventory))
        assert snapshot(S) == before
        assert verify_source_lock(S, LOCK, lock_sha) == before
        rec.update(natural_exit=0, source_after=before, secure_ram_removed=True,
                   source_unchanged=True, firmware=fw_rows,
                   host_parser=dict(path=str(H / 'out/vxgpud'), bytes=len(raw),
                                    sha256=sha(H / 'out/vxgpud')),
                   metadata_probe_sha256=sha(H / 'out/image_package_metadata'))
        save()
        print(json.dumps(dict(natural_exit=0, models=len(rec['models']),
            source_snapshot_id=before['source_snapshot_id'],
            source_lock_sha256=lock_sha, receipt_sha256=sha(E/'receipt.json')), indent=2), flush=True)
    except BaseException as error:
        rec.update(natural_exit=1, error=repr(error), traceback=traceback.format_exc(),
                   secure_ram_removed=not ram.exists())
        save()
        raise

if __name__ == '__main__':
    main()
