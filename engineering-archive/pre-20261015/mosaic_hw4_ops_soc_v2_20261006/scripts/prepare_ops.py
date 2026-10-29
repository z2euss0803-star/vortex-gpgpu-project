from pathlib import Path
import datetime,hashlib,json,shutil,struct
ROOT=Path(__file__).resolve().parents[1]
NN=ROOT.parent/'fast_style_vortex_20261002/build/xsim_ops_axi_order_v5'
BASE=ROOT.parent/'mosaic_hw4_soc_v1_20261006'
APP=ROOT.parent/'mosaic_board_v1_20261004'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def validate_pack(mem,dependencies):
    count=0
    for d in dependencies:
        a=int(d['buffer_byte_address'],16)
        for i in range(d['pack_values']):
            if mem.get(a+4*i)!=0:raise ValueError('PACK initial missing/nonzero: '+hex(a+4*i))
            count+=1
    return count
def job_bytes(mem,address):
    try:return struct.pack('<32I',*(mem[address+4*i] for i in range(32)))
    except KeyError as e:raise ValueError('Missing Job ABI word') from e
def contiguous_ranges(addresses):
    ranges=[]
    for a in sorted(addresses):
        if ranges and ranges[-1][1]==a:ranges[-1]=(ranges[-1][0],a+4)
        else:ranges.append((a,a+4))
    return ranges
def main():
    (ROOT/'assets').mkdir(exist_ok=False);(ROOT/'sw').mkdir();(ROOT/'tb').mkdir();(ROOT/'evidence').mkdir()
    mem={int(k,16)*4:int(v,16) for k,v in (line.split() for line in (NN/'memory.hex').read_text().splitlines())}
    meta=json.loads((NN/'diagnostic_manifest.json').read_text())
    assert validate_pack(mem,meta['gpu_pack_dependencies'])==18072
    for name in ['memory.hex','expected.hex','guards.hex','diagnostic_manifest.json']:
        shutil.copyfile(NN/name,ROOT/'assets'/name)
    kernel=(APP/'assets/gpu_image.bin').read_bytes()
    assert all(mem.get(0x80000000+i,0)==struct.unpack_from('<I',kernel,i)[0] for i in range(0,len(kernel),4))
    regions=[];payload=bytearray();stage=0x11000000
    for a,b in contiguous_ranges(k for k in mem if k>=0x80000000):
        assert a%16==0 and (b-a)%16==0
        data=struct.pack('<'+'I'*((b-a)//4),*(mem[k] for k in range(a,b,4)))
        regions.append(dict(gpu_address=a,ps_address=stage+len(payload),bytes=len(data),payload_offset=len(payload)))
        payload.extend(data)
    (ROOT/'assets/payload.bin').write_bytes(payload)
    jobs=[job_bytes(mem,d['arg_addr']) for d in meta['jobs']]
    (ROOT/'assets/jobs.bin').write_bytes(b''.join(jobs))
    assert all(mem[a]==0 for a in range(0,0x1000,4))
    assert all(mem[a]==0 for a in range(0x10030000,0x10030040,4))
    expected=[[] for _ in range(10)]
    for line in (NN/'expected.hex').read_text().splitlines():
        j,a,v=(int(x,16) for x in line.split());expected[j].append((a*4,v))
    for j,rows in enumerate(expected):
        assert len(rows)==meta['per_job_counts'][j]
        assert all(a==rows[0][0]+4*i for i,(a,v) in enumerate(rows))
    guards=[int(s,16)*4 for s in (NN/'guards.hex').read_text().splitlines()]
    assert len(guards)==544 and len(set(guards))==544
    for a in guards:assert mem[a]==0x5aa59669
    guard_regions=[dict(gpu_address=a,bytes=b-a) for a,b in contiguous_ranges(guards)]
    assert all(d['gpu_address']%16==0 and d['bytes']%16==0 for d in guard_regions)
    header=['/* Input descriptors only; no golden values enter device memory. */',
            'typedef struct { uint32_t gpu,ps,bytes; } Region;',
            'static const Region regions[]={']
    header += [f'{{0x{d["gpu_address"]:08x}u,0x{d["ps_address"]:08x}u,{d["bytes"]}u}},' for d in regions]
    header+=['};','static const uint32_t job_words[10][32]={']
    header += ['{'+','.join(f'0x{x:08x}u' for x in struct.unpack('<32I',b))+'},' for b in jobs]
    header+=['};','static const Region output_regions[10]={']
    header += [f'{{0x{rows[0][0]:08x}u,0x14000040u,{len(rows)*4}u}},' for rows in expected]
    header+=['};','static const Region guard_regions[]={']
    header += [f'{{0x{d["gpu_address"]:08x}u,0x14010000u,{d["bytes"]}u}},' for d in guard_regions]
    header+=['};',f'#define PAYLOAD_BYTES {len(payload)}u']
    (ROOT/'sw/fixture_inputs.h').write_text('\n'.join(header)+'\n')
    header=['/* CPU comparison only; never staged or DMA copied to GPU. */','static const uint32_t golden_offsets[11]={'+','.join(str(sum(len(x) for x in expected[:j])) for j in range(11))+'};','static const uint32_t golden_bits[24920]={']
    header += [','.join(f'0x{v:08x}u' for a,v in rows)+',' for rows in expected]
    header+=['};'];(ROOT/'sw/fixture_reference.h').write_text('\n'.join(header)+'\n')
    for n in ['mosaic_device.c','mosaic_device.h','mosaic_job.h','mosaic_addresses.h']:
        shutil.copyfile(APP/'sw'/n,ROOT/'sw'/n)
    for n in ['host_platform.h','host_dpi.h']:shutil.copyfile(BASE/'sw'/n,ROOT/'sw'/n)
    for n in ['tb_ps_host_c.sv','shared_host_adapter.svh','host_access.svh','ps_allocation_check.svh']:
        shutil.copyfile(BASE/'tb'/n,ROOT/'tb'/n)
    fixture=dict(prepared=datetime.datetime.now().astimezone().isoformat(),regions=regions,guard_regions=guard_regions,
        jobs=meta['jobs'],counts=meta['per_job_counts'],payload_bytes=len(payload),rounds=2,checks_per_round=24920,
        guards_per_round=5440,pack_initial_zero=True,reference_pack_seeded=False,kernel_image_sha256=sha(APP/'assets/gpu_image.bin'),
        source_memory_sha256=sha(NN/'memory.hex'),scope='HW4 PS VIP/native production C device driver/CDMA/actual GPU/DDR4 PHY and device model; no A53 ISA, FSBL or board')
    (ROOT/'evidence/fixture.json').write_text(json.dumps(fixture,indent=2))
    print('OPERATOR_FIXTURE_PREPARED',len(regions),'GPU allocations;',len(payload),'payload bytes;',len(guard_regions),'guard spans')
if __name__=='__main__':main()
