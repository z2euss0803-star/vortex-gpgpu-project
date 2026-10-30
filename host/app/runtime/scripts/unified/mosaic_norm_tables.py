"""Bind immutable measured norm tables; no regenerated or substitute values."""
import hashlib,json,pathlib,sys
def main():
 out=pathlib.Path(sys.argv[1]);config=pathlib.Path('config/mosaic/norm-compat-v3.json');norm=json.loads(config.read_text());rows=[];text='.section .rodata.mosaic_norm,"a"\n.balign 64\n'
 for key,symbol in [('rcp.f32','mosaic_norm_rcp_count_v2'),('rsqrt.bin','mosaic_norm_rsqrt_delta_v2')]:
  expected=norm['tables'][key];p=pathlib.Path(expected['remote_path']);raw=p.read_bytes();digest=hashlib.sha256(raw).hexdigest();assert len(raw)==expected['bytes'] and digest==expected['sha256']
  path=str(p.resolve());assert not any(x in path for x in ('"','\n','\\'))
  text+='.global '+symbol+'\n.type '+symbol+',%object\n'+symbol+':\n.incbin "'+path+'"\n.size '+symbol+', .-'+symbol+'\n';rows.append(dict(path=path,bytes=len(raw),sha256=digest))
 text+='.section .note.GNU-stack,"",%progbits\n';out.write_bytes(text.encode());out.with_suffix('.json').write_bytes((json.dumps(dict(scope='FROZEN_READ_ONLY_TABLES_NOT_GPU',config_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),tables=rows,gpu_raw_cycles=None),indent=2)+'\n').encode())
if __name__=='__main__':main()
