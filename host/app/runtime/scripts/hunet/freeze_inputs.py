"""Bind the approved v5/HW4 sources by real bytes. Portable manifests, no device access."""
import argparse, hashlib, json
from pathlib import Path, PureWindowsPath, PurePosixPath

def sha(data): return hashlib.sha256(data).hexdigest()
def read_json(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def resolve_source(root, relative):
    p=PurePosixPath(relative)
    if p.is_absolute() or '..' in p.parts or ':' in relative or '\\' in relative:
        raise ValueError('SOURCE_PATH_ESCAPE')
    resolved=(root/relative).resolve()
    try: resolved.relative_to(root.resolve())
    except ValueError: raise ValueError('SOURCE_PATH_ESCAPE')
    return resolved
def portable_source(path):
    return PureWindowsPath(path).relative_to(PureWindowsPath('F:/obsidian-codex/codex')).as_posix()
def validate_freeze(manifest, source_root):
    errors=[];root=Path(source_root)
    for item in manifest.get('files',[]):
        try:
            p=resolve_source(root,item['path']);data=p.read_bytes()
            if len(data)!=item['bytes'] or sha(data)!=item['sha256']:
                errors.append(dict(path=item['path'],reason='CONTENT_MISMATCH'))
        except (ValueError,OSError,KeyError) as exc:
            errors.append(dict(path=item.get('path'),reason=str(exc)))
    if manifest.get('profile',{}).get('id')!=4:errors.append(dict(reason='PROFILE_MISMATCH'))
    return errors
def freeze_inputs(source_root, out_dir):
    root=Path(source_root);out=Path(out_dir)
    audit=read_json(root/'work/hunet_linux_backend_review_20261011/source-and-crt-audit.json')
    if audit['errors'] or audit['delivery_file_checks']!=285:raise ValueError('UNAPPROVED_AUDIT')
    files={}
    for item in audit['files']:
        rel=portable_source(item['path'])
        files[rel]=dict(path=rel,bytes=item['bytes'],sha256=item['sha256'],role=item['role'])
    roi=read_json(root/'work/hunet_original_tcu_20261008_v1/data/roi_manifest.json')
    inputs=[];valid=[];excluded=[]
    for index,record in enumerate(roi['records']):
        if record['status']=='include': valid.append(index)
        else: excluded.append(index)
        p=root/'work/hunet_original_tcu_20261008_v1/data/board_inputs'/(record['id']+'.f32')
        expected=record['preprocess']['tensor_sha256']
        rel=p.relative_to(root).as_posix()
        files[rel]=dict(path=rel,bytes=196608,sha256=expected,role='roi_input')
        inputs.append(dict(roi_index=index,roi_id=record['id'],included=record['status']=='include',
                           path=rel,bytes=196608,sha256=expected))
    profile=dict(id=4,name='hw4-hunet-v5',control=0xa0000000,cdma=0xb0010000,ready_gpio=0xb0020000,
        mmio_bytes=65536,ps_base=0x10000000,ps_size=0x100000,dma_bits=32,
        gpu_pl_base=0x80000000,gpu_pl_end=0x100000000,pl_system_base=0x500000000,
        pl_gpu_visible_end=0x580000000,pl_physical_end=0x600000000,
        bit_sha256='deb74c77e7e76e9108f49307371c42c889b21f8e764c545987a7786ce543ebc8',
        xsa_sha256='7fdd711f0229ee01d369188d44852c0f85193aada485ac4b1998e2b4e3e32a91',
        topology=dict(clusters=1,cores=4,warps=4,threads=4),program_ids=[6,7],
        watchdog=4000000000,execution_ms=60000,network_ms=180000,
        cdma_policy=dict(alignment=16,chunk=4096,dre=False,sg=False,polling=True),
        private=dict(crt_base=0x10002000,crt_bytes=4096,console=0x10002040,csr=0x10002080,
                     args_base=0x10010040,args_bytes=128,bounce_base=0x100ff000,bounce_bytes=4096,
                     stack_base=0xfff00000,stack_end=0x100000000),
        evidence_scope='STATIC_SOURCE_AND_LINUX_POLICY_NOT_BOARD')
    h0=read_json(root/'work/vortex_linux_backend_cnn_i32_20261010/contracts/profiles/h0-r4.json')
    protected=[]
    parent=root/'work/vortex_linux_backend_cnn_i32_20261010'
    for p in sorted(list((parent/'vendor/h0').glob('*'))+[parent/'generated/cnn/programs.h',parent/'contracts/profiles/h0-r4.json']):
        if p.is_file():
            protected.append(dict(path=p.relative_to(root).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()),role='protected_h0_parent'))
    for f in protected: files[f['path']]=f
    frozen=dict(schema=1,source_parent_commit='c8d88f5aacb8fb06ad0df7f5752744a62d587a0e',
        upstream_commit=audit['upstream_commit'],author_commit=audit['author_commit'],
        profile=profile,delivery_count=285,v5_manifest_sha256=audit['v5_manifest_sha256'],
        files=sorted(files.values(),key=lambda x:x['path']),
        graph=dict(jobs=1023,stages=100,matrices=314,jobs_bytes=130944,matrices_bytes=10048,
                   op_counts=audit['hunet_assets']['op_counts'],layout=audit['hunet_assets']['regions']),
        inputs=dict(records=inputs,valid_roi_indices=valid,excluded_truncated=excluded),
        protected_parents=dict(h0_bit_sha256=h0['bit_sha256'],h0_source_commit='c8d88f5aacb8fb06ad0df7f5752744a62d587a0e',files=protected),
        statuses=dict(product='NOT_RUN',rv32_linux='NOT_RUN',aarch64='NOT_RUN',image='NOT_RUN',hw4_linux='NOT_RUN',h1='NOT_RUN'))
    errors=validate_freeze(frozen,root)
    if errors:raise ValueError(json.dumps(errors,ensure_ascii=True))
    out.mkdir(parents=True,exist_ok=True)
    for relative,value in [('hunet_v5.json',frozen),('profiles/hw4-hunet-v5.json',profile),('hunet_inputs.json',frozen['inputs'])]:
        p=out/relative;p.parent.mkdir(parents=True,exist_ok=True)
        with p.open('w',encoding='utf-8',newline='\n') as stream:
            stream.write(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
    return frozen
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source-root',required=True);p.add_argument('--out',required=True)
    a=p.parse_args();m=freeze_inputs(Path(a.source_root),Path(a.out))
    print(json.dumps(dict(scope='STATIC_FREEZE',files=len(m['files']),delivery=m['delivery_count'],valid_rois=len(m['inputs']['valid_roi_indices']))))
