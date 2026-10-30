"""Derive an isolated native-ext4 project; never deploy or modify the baseline."""
import argparse,datetime,hashlib,json,shutil,subprocess
from pathlib import Path
from image_audit import identity

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def check_paths(old,base,snapshot,payload):
 old=old.resolve();base=base.resolve();snapshot=snapshot.resolve();payload=payload.resolve()
 if old.name!='env_backend' or base.parent!=old.parent.parent or base==old.parent or base.exists():
  raise ValueError('Refuse existing/overlapping/outside sibling candidate')
 if old in base.parents or base in old.parents:raise ValueError('Overlapping projects')
 if not (snapshot/'SOURCE_COMMIT').is_file() or not payload.is_file():raise ValueError('Missing sealed snapshot/package')
 return old,base,snapshot,payload

def prepare(old,base,snapshot,payload,expected):
 old,base,snapshot,payload=check_paths(old,base,snapshot,payload)
 if sha(payload)!=expected:raise ValueError('Source archive SHA')
 commit=(snapshot/'SOURCE_COMMIT').read_text().strip()
 inventory=json.loads((snapshot/'SOURCE_SHA256.json').read_text())
 for name,digest in inventory.items():
  if sha(snapshot/name)!=digest:raise ValueError('Snapshot changed '+name)
 protected={str(p):sha(p) for folder in ('project-spec/configs','project-spec/meta-user','images/linux') for p in (old/folder).rglob('*') if p.is_file()}
 base.mkdir();(base/'protected-original.json').write_text(json.dumps(protected,indent=2)+'\n')
 new=base/'env_backend'
 def ignore(path,names):return [n for n in names if n=='.git' or n.endswith('.log') or (Path(path)==old and n in ('build','images'))]
 shutil.copytree(str(old),str(new),ignore=ignore);(new/'build').mkdir()
 for name in ('downloads','sstate-cache'):
  subprocess.check_call(['cp','-a','--reflink=auto',str(old/'build'/name),str(new/'build'/name)])
 meta=new/'project-spec/meta-user'
 for name in ('recipes-kernel/linux/files/vxl-source.tar.gz','recipes-apps/vxl-runtime/files/vxl-source.tar.gz'):
  shutil.copy2(str(payload),str(meta/name))
 kernel=meta/'recipes-kernel/linux/linux-xlnx_%.bbappend'
 text=kernel.read_text()
 needle='${WORKDIR}/vxl-source/include ${S}/drivers/misc/vxl/'
 if text.count(needle)!=1:raise ValueError('Unexpected baseline kernel recipe')
 kernel.write_text(text.replace(needle,'${WORKDIR}/vxl-source/include ${WORKDIR}/vxl-source/generated ${S}/drivers/misc/vxl/'))
 app=meta/'recipes-apps/vxl-runtime/vxl-runtime.bb';text=app.read_text()
 text=text.replace('CXX="${CXX}" build/', 'CXX="${CXX}" SOURCE_COMMIT='+commit+' build/')
 text=text.replace('build/vxl_vecadd\n','build/vxl_vecadd build/vxl_cnn\n')
 text=text.replace('build/vxl_vecadd ${D}${bindir}/','build/vxl_vecadd build/vxl_cnn ${D}${bindir}/')
 text=text.replace('    install -d ${D}${libdir} ${D}${bindir}','    install -d ${D}${libdir} ${D}${bindir} ${D}${datadir}/codex-cnn\n    install -m 0644 kernel/cnn/id*.bin ${D}${datadir}/codex-cnn/')
 text=text.replace('FILES_${PN} += "${libdir}/*.so"','FILES_${PN} += "${libdir}/*.so ${datadir}/codex-cnn"')
 if 'SOURCE_COMMIT='+commit not in text or 'build/vxl_cnn ${D}${bindir}/' not in text:raise ValueError('Runtime integration absent')
 app.write_text(text)
 build='H0-LINUX-CNN-I32-V1-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ')
 first=meta/'recipes-apps/codex-firstboot';(first/'files/codex-build-id').write_text(build+'\n')
 (first/'files/codex-cnn-manifest.json').write_text(json.dumps(identity(commit,build),indent=2)+'\n')
 recipe=first/'codex-firstboot.bb';text=recipe.read_text().replace('file://codex-build-id"','file://codex-build-id file://codex-cnn-manifest.json"')
 text=text.replace('    install -m 0644 ${WORKDIR}/codex-build-id ${D}${sysconfdir}/codex-build-id','    install -m 0644 ${WORKDIR}/codex-build-id ${D}${sysconfdir}/codex-build-id\n    install -m 0644 ${WORKDIR}/codex-cnn-manifest.json ${D}${sysconfdir}/codex-cnn-manifest.json')
 recipe.write_text(text)
 boot=old/'images/linux/BOOT.BIN'
 if not boot.is_file():
  boot=old.parent/'release/BOOT.BIN'
 if not boot.is_file():raise ValueError('Protected matching BOOT.BIN absent')
 protected[str(boot)]=sha(boot);(base/'protected-original.json').write_text(json.dumps(protected,indent=2)+'\n')
 integration=dict(scope='CNN_ISOLATED_IMAGE_BUILD',source_commit=commit,build_id=build,payload_sha256=expected,source_project=str(old),derived_project=str(new),snapshot=str(snapshot),boot_bin=str(boot),boot_bin_sha256=sha(boot),board='NOT_RUN',sd='NOT_TOUCHED')
 (base/'integration.json').write_text(json.dumps(integration,indent=2)+'\n');(base/'build-id').write_text(build+'\n')
 for name,digest in protected.items():
  if sha(Path(name))!=digest:raise ValueError('Protected original changed '+name)
 print('CNN_IMAGE_PROJECT_PREPARED',new,build,flush=True)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--old',required=True);p.add_argument('--base',required=True);p.add_argument('--snapshot',required=True);p.add_argument('--payload',required=True);p.add_argument('--sha256',required=True);o=p.parse_args()
 prepare(Path(o.old),Path(o.base),Path(o.snapshot),Path(o.payload),o.sha256)
