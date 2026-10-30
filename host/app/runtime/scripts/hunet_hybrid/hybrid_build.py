"""Hybrid source snapshot and actual compiler evidence. No media access."""
import importlib.util,json,pathlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import source_snapshot,elf_record
def package_module():
 s=importlib.util.spec_from_file_location('hybrid_build_package',ROOT/'scripts/hunet_hybrid/package_release.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def checked_hybrid(release):
 release=pathlib.Path(release).resolve();r=package_module().verify_hybrid_release(release)
 if r.get('integrity')!='PASS' or r.get('scope')!='HUNET_HYBRID_OFFLINE_RELEASE':raise ValueError('ACTUAL_HYBRID_RELEASE_REQUIRED:'+str(r))
 hybrid=json.loads((release/'release-hybrid.json').read_bytes());base=json.loads((release/'base/release.json').read_bytes())
 return dict(base,**{k:v for k,v in hybrid.items() if k!='files'},hybrid_files=hybrid['files'])
def hybrid_snapshot(release,destination):
 root=pathlib.Path(release);source=source_snapshot(root/'base',destination)
 header=(root/'build/vxl_hybrid_release.h').read_bytes()
 for name in ['build/vxl_hybrid_release.h','driver/vxl_hybrid_release.h']:(source/name).write_bytes(header)
 return source
