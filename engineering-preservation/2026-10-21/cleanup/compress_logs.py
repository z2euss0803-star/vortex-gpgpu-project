"""Reversible NTFS compression of explicit inactive text logs; no deletion.

Keep path, logical bytes and SHA256 unchanged. Preserve command/exit/allocation
and free-space deltas separately. Stop on an error or any byte-identity drift.
"""
from pathlib import Path
import ctypes,datetime,getpass,hashlib,json,os,platform,shutil,subprocess
from ctypes import wintypes

ROOT=Path('F:/obsidian-codex/codex').resolve()
OUT=Path(__file__).resolve().parent/'evidence'
TARGETS=[Path(x) for x in ['F:\\obsidian-codex\\codex\\work\\mosaic_tcu_expand_20261007_v1\\candidates\\e8\\runs\\fullsynth_area_medium_v1\\hw\\evidence\\primitive_inventory.txt', 'F:\\obsidian-codex\\codex\\work\\mosaic_tcu_expand_20261007_v1\\evidence\\fullsynth_area_medium_v1_terminal_scene_v1\\run\\hw\\evidence\\primitive_inventory.txt', 'F:\\obsidian-codex\\codex\\work\\mosaic_tcu_expand_20261007_v1\\candidates\\e8\\runs\\fullsynth_v2\\hw\\evidence\\primitive_inventory.txt', 'F:\\obsidian-codex\\codex\\work\\mosaic_tcu_expand_20261007_v1\\evidence\\fullsynth_v2_tracking_failure_scene_v1\\run\\hw\\evidence\\primitive_inventory.txt', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c4w4t8\\render128\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c4w8t4\\render128\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c4w4t4\\render128\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c2w4t4\\render128\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c1w4t4\\render128\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c4w8t4\\cnn\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c4w4t8\\cnn\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c4w4t4\\cnn\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c2w4t4\\cnn\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\tmp\\vortex_config_20260913\\runs\\bank1-r1\\c1w4t4\\cnn\\trace\\ramulator.log.ch0', 'F:\\obsidian-codex\\codex\\work\\mosaic_tcu_dual_s8_shared_b_20261018_v1\\runs\\20261020T215251688659Z_candidate_gpu_pair\\phase_3.log', 'F:\\obsidian-codex\\codex\\work\\mosaic_tcu_dual_s8_shared_b_20261018_v1\\runs\\20261020T215251688659Z_candidate_gpu_pair\\simulate.log']]

api=ctypes.WinDLL('kernel32',use_last_error=True)
api.GetCompressedFileSizeW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(wintypes.DWORD)]
api.GetCompressedFileSizeW.restype=wintypes.DWORD

def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(path,obj):path.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024**2),b''):h.update(block)
    return h.hexdigest()
def allocated(path):
    high=wintypes.DWORD(); ctypes.set_last_error(0)
    low=api.GetCompressedFileSizeW(str(path),ctypes.byref(high))
    if low==0xffffffff and ctypes.get_last_error():raise ctypes.WinError(ctypes.get_last_error())
    return (high.value<<32)|low

def main():
    assert platform.node().upper()=='RY' and getpass.getuser()=='13151'
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    run=OUT/(stamp+'_ntfs_log_compression'); run.mkdir(exist_ok=False)
    receipt={'started_utc':utc(),'host':platform.node(),'account':getpass.getuser(),'action':'NTFS_lossless_compression_no_delete_no_move','mode':'active','disk_free_before':shutil.disk_usage(ROOT).free,'files':[]}
    save(run/'receipt.json',receipt)
    for i,target in enumerate(TARGETS):
        p=target.resolve(strict=True)
        assert p.is_relative_to(ROOT) and p==target
        stat_before=p.stat(); assert not getattr(stat_before,'st_file_attributes',0)&0x400
        with p.open('rb') as f:head=f.read(4096)
        assert head and b'\0' not in head
        before={'path':str(p),'logical_bytes':stat_before.st_size,'allocated_bytes':allocated(p),'sha256':digest(p),'mtime_ns':stat_before.st_mtime_ns}
        if stat_before.st_mtime_ns!=p.stat().st_mtime_ns:raise ValueError('File changed during pre-hash')
        save(run/f'{i}_before.json',before)
        command=[str(Path(os.environ['SystemRoot'])/'System32/compact.exe'),'/C','/A','/Q',str(p)]
        started=utc(); result=subprocess.run(command,capture_output=True)
        (run/f'{i}_compact_stdout.bin').write_bytes(result.stdout); (run/f'{i}_compact_stderr.bin').write_bytes(result.stderr)
        after_stat=p.stat()
        after={'logical_bytes':after_stat.st_size,'allocated_bytes':allocated(p),'sha256':digest(p),'mtime_ns':after_stat.st_mtime_ns,'compressed_attribute':bool(getattr(after_stat,'st_file_attributes',0)&0x800)}
        same=after['sha256']==before['sha256'] and after['logical_bytes']==before['logical_bytes'] and after['mtime_ns']==before['mtime_ns']
        entry={'started_utc':started,'finished_utc':utc(),'command':command,'exit_code':result.returncode,'before':before,'after':after,'byte_identity_unchanged':same,'allocation_saved_bytes':before['allocated_bytes']-after['allocated_bytes'],'status':'PASS' if result.returncode==0 and same else 'FAIL_OR_UNKNOWN'}
        receipt['files'].append(entry); save(run/'receipt.json',receipt)
        print(json.dumps({'file':str(p),'status':entry['status'],'saved_gib':round(entry['allocation_saved_bytes']/2**30,3),'logical_gib':round(before['logical_bytes']/2**30,3),'sha256':before['sha256']}),flush=True)
        if entry['status']!='PASS':raise ValueError('Compression failed or identity drift; preserve and stop')
    receipt.update(finished_utc=utc(),mode='completed',status='LOSSLESS_COMPRESSION_HASH_VERIFIED',disk_free_after=shutil.disk_usage(ROOT).free,total_allocation_saved_bytes=sum(x['allocation_saved_bytes'] for x in receipt['files']))
    receipt['disk_free_observed_delta_bytes']=receipt['disk_free_after']-receipt['disk_free_before']
    save(run/'receipt.json',receipt)
    save(OUT/'compression_latest.json',{'run':str(run),'receipt':str(run/'receipt.json')})
    print(json.dumps({k:receipt[k] for k in ['status','total_allocation_saved_bytes','disk_free_before','disk_free_after','disk_free_observed_delta_bytes']}),flush=True)

if __name__=='__main__':main()
