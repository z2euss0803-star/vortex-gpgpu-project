"""Compile only against the named target tree; retain failed build evidence."""
import argparse
import pathlib
import re
from build_common import captured, checked_release, compiler_identity, elf_record, native_output, readelf, sha, source_snapshot, write_json
from cross_audit import audit_kernel


def build(release_dir, out_dir, prefix, kernel_source, kernel_build, actual=False):
    if (pathlib.Path(release_dir)/'release-hybrid.json').is_file():
        import sys
        sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'hunet_hybrid'))
        from hybrid_cross import build_kernel
        return build_kernel(release_dir,out_dir,prefix,kernel_source,kernel_build,actual)
    manifest = checked_release(release_dir); output = native_output(out_dir)
    kernel_source=pathlib.Path(kernel_source).resolve();kernel_build=pathlib.Path(kernel_build).resolve()
    record=dict(scope='ACTUAL_IMAGE_KERNEL_CROSS' if actual else 'PRELIMINARY_KERNEL_CROSS',
        exit=None, source_commit=manifest['source_commit'], source_digest=manifest['source_digest'],
        source_tree=str(kernel_source), kernel_build=str(kernel_build), W=1, Werror=True,
        board='NOT_RUN', sd='NOT_TOUCHED')
    try:
        compiler=compiler_identity(prefix);record['compiler']=compiler['version'];record['toolchain']=compiler
        config=kernel_build/'.config';symvers=kernel_build/'Module.symvers'
        record['config']=config.read_text().splitlines();record['config_sha256']=sha(config);record['symvers_sha256']=sha(symvers)
        record['kernel_release']=(kernel_build/'include/config/kernel.release').read_text().strip()
        if not record['kernel_release'].startswith('5.4.'): raise ValueError('MATCHING_KERNEL_REQUIRED')
        source=source_snapshot(release_dir,output/'source')
        command=['make','-C',str(kernel_source),'O='+str(kernel_build),'ARCH=arm64','CROSS_COMPILE='+prefix,'M='+str(source/'driver'),'W=1','modules']
        record['command']=command;captured(command,output/'build.log');record['exit']=0
        module=source/'driver/vxl_linux.ko';value=elf_record(prefix+'readelf',module,manifest)
        record['arch']=value['machine'];record['module_sha256']=sha(module)
        record['source_bound']=value['source_digest']==manifest['source_digest'] and value['source_commit']==manifest['source_commit']
        info=readelf(prefix+'readelf',module,'-p','.modinfo');match=re.search(r'vermagic=([^\n]+)',info)
        record['vermagic']=match.group(1).strip() if match else ''
        if sha(config)!=record['config_sha256'] or sha(symvers)!=record['symvers_sha256']: raise ValueError('KERNEL_INPUT_CHANGED')
    except Exception as error: record['error']=str(error)
    result=audit_kernel(record,manifest);record['audit']=result;write_json(output/'receipt.json',record)
    if result['integrity']!='PASS': raise ValueError(result)
    print(record['scope']+'_PASS_NOT_BOARD',output,flush=True)
    return record


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--release',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True)
    p.add_argument('--cross-compile',required=True);p.add_argument('--kernel-source',required=True);p.add_argument('--kernel-build',required=True);p.add_argument('--actual-image-kernel',action='store_true');a=p.parse_args()
    build(a.release,a.out,a.cross_compile,a.kernel_source,a.kernel_build,a.actual_image_kernel)
