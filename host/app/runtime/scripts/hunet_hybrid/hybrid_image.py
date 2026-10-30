"""Fresh mixed-image preparation; keep the frozen HW4 parent and BOOT.BIN."""
import datetime,hashlib,importlib.util,json,pathlib,re,shutil,sys
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from build_common import sha,write_json
from cross_audit import HYBRID_SYMBOLS,audit_hybrid_cross
from hybrid_build import checked_hybrid
def legacy():
 s=importlib.util.spec_from_file_location('hybrid_parent_prepare',ROOT/'scripts/hunet/prepare_image.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def recipes_hybrid(commit,license_md5):
 r=legacy().recipes(commit,license_md5)
 r['kernel']=r['kernel'].replace('file://vxl_hunet_source.h file://vxl-built-in.cfg','file://vxl_hunet_source.h file://vxl_hybrid_release.h file://vxl-built-in.cfg').replace('    sed -i', '    install -m 0644 ${WORKDIR}/vxl_hybrid_release.h ${S}/drivers/misc/vxl/driver/\n    sed -i',1)
 r['apps']=r['apps'].replace('build/vxl_hunet_wmma\n','build/vxl_hunet_wmma build/libvortex-hybrid.so\n',1)
 r['apps']=r['apps'].replace('    cp -R --no-preserve=ownership','    install -m 0755 build/libvortex-hybrid.so ${D}${libdir}/\n    install -m 0755 ${WORKDIR}/hunet-release/cpu/gelu.elf ${D}${bindir}/vxl_hunet_hybrid\n    cp -R --no-preserve=ownership',1)
 r['apps']=r['apps'].replace('${datadir}/codex-hunet-v5/program6/kernel.elf ${datadir}/codex-hunet-v5/program7/kernel.elf','${datadir}/codex-hunet-v5/base/program6/kernel.elf ${datadir}/codex-hunet-v5/base/program7/kernel.elf ${datadir}/codex-hunet-v5/program8/kernel.elf')
 # CPU self identity binds exact ELF bytes, including the debug sections.
 # These recipe-local gates preserve both copies of that authenticated asset.
 r['apps']+='\nINHIBIT_PACKAGE_STRIP = "1"\nINHIBIT_PACKAGE_DEBUG_SPLIT = "1"\n'
 return r
def hybrid_image_errors(record,release):
 errors=[]
 for key,wanted in [('hybrid_binding_sha256',release['binding_sha256']),('cpu_elf_sha256',release['cpu_binary_sha256']),('program8_code_sha256',release['program_sha256'])]:
  if record.get(key)!=wanted:errors.append('hybrid image '+key)
 if not set(HYBRID_SYMBOLS).issubset(record.get('hybrid_builtin_symbols',[])):errors.append('hybrid built-in symbols')
 if record.get('hybrid_library_source_bound') is not True:errors.append('hybrid library source binding')
 if not record.get('runtime_libm_sha256') or record['runtime_libm_sha256']!=record.get('cross_libm_sha256'):errors.append('CPU libm identity')
 return errors
def prepare_hybrid(base,candidate,release_dir,xsa,cache_project=None):
 release_dir=pathlib.Path(release_dir).resolve();release=checked_hybrid(release_dir);cross_path=release_dir.parent/'cross/receipt.json';cross=json.loads(cross_path.read_bytes())
 if audit_hybrid_cross(cross,release)['integrity']!='PASS':raise ValueError('ACTUAL_HYBRID_CROSS_GATE')
 parent=pathlib.Path(base).resolve();boot=parent/'images/linux/BOOT.BIN'
 # The package contains exactly the same frozen HW4 PL. Do not rebuild FSBL/PL.
 s=importlib.util.spec_from_file_location('hybrid_boot_audit',ROOT/'scripts/hunet/image_audit.py');a=importlib.util.module_from_spec(s);s.loader.exec_module(a)
 if a.boot_bit_errors(boot.read_bytes(),a.bit_payload((release_dir/'base/hardware/top.bit').read_bytes())):raise ValueError('PARENT_FROZEN_HW4_BOOT_REQUIRED')
 old=legacy();integration=old.prepare_image(base,candidate,release_dir/'base',xsa,cache_project);dest=pathlib.Path(candidate).resolve();project=dest/'env_backend';meta=project/'project-spec/meta-user';source=dest/'sealed-source'
 header=(release_dir/'build/vxl_hybrid_release.h').read_bytes()
 for name in ['driver/vxl_hybrid_release.h','build/vxl_hybrid_release.h']:(source/name).write_bytes(header)
 r=recipes_hybrid(release['source_commit'],hashlib.md5((source/'vendor/vortex/LICENSE').read_bytes()).hexdigest())
 (meta/'recipes-kernel/linux/linux-xlnx_%.bbappend').write_text(r['kernel']);(meta/'recipes-apps/vxl-runtime/vxl-runtime.bb').write_text(r['apps'])
 (meta/'recipes-kernel/linux/files/vxl_hybrid_release.h').write_bytes(header)
 # Replace only the fresh candidate's archive with the complete verified hybrid release.
 import io,tarfile
 archive=meta/'recipes-apps/vxl-runtime/files/hunet-release.tar.gz'
 with tarfile.open(str(archive),'w:gz') as tar:
  for name in sorted(set(release['hybrid_files'])|{'release-hybrid.json'}):
   data=(release_dir/name).read_bytes();entry=tarfile.TarInfo(name);entry.size=len(data);entry.mode=0o644;entry.mtime=0;tar.addfile(entry,io.BytesIO(data))
 build_id='HW4-HUNET-HYBRID-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
 manifest=dict(scope='HW4_HUNET_HYBRID_LINUX_CANDIDATE_V1',profile=4,program_id=8,build_id=build_id,sd_variant=old.SD_VARIANT,**{k:release[k] for k in ['source_commit','source_digest','bit_sha256','xsa_sha256','binding_sha256','cpu_binary_sha256']})
 first=meta/'recipes-apps/codex-firstboot/files';(first/'codex-build-id').write_text(build_id+'\n');write_json(first/'codex-hunet-manifest.json',manifest)
 config=project/'project-spec/configs/config';text,n=re.subn(r'^CONFIG_SUBSYSTEM_HOSTNAME=.*$','CONFIG_SUBSYSTEM_HOSTNAME="codex-hw4-hunet-hybrid"',config.read_text(),flags=re.M)
 if n!=1:raise ValueError('HOSTNAME_CONFIG')
 config.write_text(text);shutil.copy2(boot,dest/'retained-BOOT.BIN')
 integration.update(scope='HW4_HUNET_HYBRID_ISOLATED_IMAGE_BUILD',manifest=manifest,release_dir=str(release_dir),cross_receipt=str(cross_path),cross_receipt_sha256=sha(cross_path),retained_boot_sha256=sha(boot),cpu_binary_install='EXACT_CROSS_ELF_FROM_RELEASE_NO_RECOMPILE')
 write_json(dest/'integration.json',integration);print('HW4_HUNET_HYBRID_PREPARED_NOT_BOARD',dest,build_id,flush=True);return integration
