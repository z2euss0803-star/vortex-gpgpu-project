"""Wrap the native historical renderer; this never publishes a live result."""
import argparse, hashlib, json, pathlib, struct, subprocess, zlib

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

def render(binary, out):
    out = pathlib.Path(out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    p = subprocess.run([str(pathlib.Path(binary).resolve()), str(out)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (out / 'stdout.bin').write_bytes(p.stdout)
    (out / 'stderr.bin').write_bytes(p.stderr)
    (out / 'exit.txt').write_text(str(p.returncode) + '\n')
    if p.returncode:
        raise RuntimeError('native historical renderer failed')
    m = json.loads((out / 'frame.json').read_text())
    data = (out / 'hunet-historical.rgb').read_bytes()
    if m['scope'] != 'OFFSCREEN_UNPUBLISHED_HISTORICAL_VIEW_NOT_DP_NOT_INFERENCE' or m['raw_gpu_cycles'] is not None:
        raise ValueError('historical fixture scope required')
    if len(data) != m['bytes'] or hashlib.sha256(data).hexdigest() != m['sha256']:
        raise ValueError('native RGB identity mismatch')
    w, h, stride = m['width'], m['height'], m['stride']
    if not (0 < w <= 1920 and 0 < h <= 1080 and stride >= w * 3 and len(data) == h * stride):
        raise ValueError('native RGB geometry invalid')
    scan = b''.join(b'\0' + data[y * stride:y * stride + w * 3] for y in range(h))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b'')
    (out / 'hunet-historical.png').write_bytes(png)
    return m

if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--binary', required=True); p.add_argument('--out', required=True)
    a = p.parse_args(); print(json.dumps(render(a.binary, a.out)))
