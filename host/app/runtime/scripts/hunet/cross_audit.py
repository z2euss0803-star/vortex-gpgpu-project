"""Validate recorded cross-build facts. Offline evidence is never board PASS."""
import re

ARTIFACTS = ('libvortex-hunet.so', 'vxl_hunet', 'vxl_hunet_copy', 'vxl_hunet_wmma')
HYBRID_OBJECTS=('shared_hunet_hybrid_state.o','shared_hunet_hybrid_read.o','shared_hunet_hybrid_write.o','shared_hunet_hybrid_evidence.o','vxl_hunet.o')
HYBRID_SYMBOLS=('vxl_hybrid_enter_cpu','vxl_hybrid_begin_cpu','vxl_hybrid_commit_cpu','vxl_hybrid_get_cpu_evidence','vxl_hybrid_end')

def audit_hybrid_cross(record,release):
    legacy=dict(record,scope='HUNET_AARCH64_CROSS',elfs={k:v for k,v in record.get('elfs',{}).items() if k!='vxl_hunet_hybrid'})
    errors=audit_cross(legacy,release)['errors']
    if record.get('scope')!='HUNET_HYBRID_AARCH64_CROSS':errors.append('hybrid scope')
    if record.get('binding_sha256')!=release.get('binding_sha256'):errors.append('hybrid binding')
    if record.get('verified_source_snapshot') is not True:errors.append('unverified CPU source snapshot')
    if 'libvortex-hybrid.so' not in record.get('elfs',{}):errors.append('missing hybrid library')
    cpu=record.get('elfs',{}).get('vxl_hunet_hybrid',{})
    if cpu.get('binding_kind')!='RELEASE_SHA_AND_VERIFIED_SOURCE_SNAPSHOT' or cpu.get('sha256')!=release.get('cpu_binary_sha256'):errors.append('CPU release SHA/source binding')
    if cpu.get('machine')!='AArch64' or cpu.get('interpreter')!='/lib/ld-linux-aarch64.so.1':errors.append('CPU ELF/loader')
    needed=cpu.get('needed',[])
    if not {'libm.so.6','libpthread.so.0','libdl.so.2'}.issubset(needed) or not set(needed).issubset(record.get('rootfs_libraries',[])):errors.append('CPU runtime libraries')
    if not set(cpu.get('required_versions',[])).issubset(record.get('runtime_versions',[])):errors.append('CPU runtime versions')
    return dict(integrity='FAIL' if errors else 'PASS',errors=errors,scope='HYBRID_AARCH64_CROSS_ONLY',board='NOT_RUN')

def audit_hybrid_kernel(record,release):
    errors=audit_kernel(record,release)['errors']
    if record.get('hybrid_binding_sha256')!=release.get('binding_sha256'):errors.append('module hybrid binding')
    if set(record.get('hybrid_objects',[]))!=set(HYBRID_OBJECTS):errors.append('missing hybrid objects')
    if not set(HYBRID_SYMBOLS).issubset(record.get('hybrid_symbols',[])):errors.append('missing hybrid symbols')
    return dict(integrity='FAIL' if errors else 'PASS',errors=errors,scope=record.get('scope'),board='NOT_RUN')


def digest_ok(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) and value != '0'*64


def binding_errors(record, release):
    return [key for key in ('source_commit', 'source_digest')
            if record.get(key) != release.get(key) or not record.get(key)]


def audit_cross(record, release):
    errors = binding_errors(record, release)
    if record.get('error'): errors.append('aborted audit: '+str(record['error']))
    if record.get('scope') != 'HUNET_AARCH64_CROSS': errors.append('scope')
    if type(record.get('exit')) is not int or record['exit'] != 0: errors.append('build exit')
    compiler = record.get('compiler', {})
    if compiler.get('target') != 'aarch64-xilinx-linux': errors.append('compiler target')
    if not re.search(r'\b9\.2\.0\b', compiler.get('version', '')): errors.append('compiler version')
    for key in ('gcc_sha256', 'gxx_sha256'):
        if not digest_ok(compiler.get(key)): errors.append(key)
    elfs = record.get('elfs', {})
    if not set(ARTIFACTS).issubset(elfs): errors.append('missing tools/library')
    for name, value in elfs.items():
        errors += [name+': '+e for e in binding_errors(value, release)]
        if value.get('machine') != 'AArch64': errors.append(name+': architecture')
        if not name.endswith('.so') and value.get('interpreter') != '/lib/ld-linux-aarch64.so.1': errors.append(name+': loader')
        if not digest_ok(value.get('sha256')): errors.append(name+': SHA')
        if not value.get('needed') or not set(value['needed']).issubset(record.get('rootfs_libraries', [])): errors.append(name+': NEEDED')
        if not set(value.get('required_versions', [])).issubset(record.get('runtime_versions', [])): errors.append(name+': runtime version')
    return dict(integrity='FAIL' if errors else 'PASS', errors=errors, scope='AARCH64_CROSS_ONLY', board='NOT_RUN')


def audit_kernel(record, release):
    errors = binding_errors(record, release)
    if record.get('error'): errors.append('aborted audit: '+str(record['error']))
    if record.get('scope') not in ('PRELIMINARY_KERNEL_CROSS', 'ACTUAL_IMAGE_KERNEL_CROSS'): errors.append('kernel scope')
    if type(record.get('exit')) is not int or record['exit'] != 0: errors.append('kernel build exit')
    version = record.get('kernel_release', '')
    if not version.startswith('5.4.') or not record.get('vermagic', '').startswith(version+' '): errors.append('kernel/vermagic')
    if record.get('arch') != 'AArch64' or not re.search(r'\b9\.2\.0\b', record.get('compiler', '')): errors.append('kernel architecture/compiler')
    if record.get('W') != 1 or record.get('Werror') is not True: errors.append('W=1/Werror')
    if record.get('source_bound') is not True: errors.append('unbound module')
    if not {'CONFIG_ZONE_DMA32=y', 'CONFIG_64BIT=y', 'CONFIG_ARM64=y'}.issubset(record.get('config', [])): errors.append('kernel config')
    for key in ('config_sha256', 'symvers_sha256', 'module_sha256'):
        if not digest_ok(record.get(key)): errors.append(key)
    for key in ('source_tree', 'kernel_build'):
        if not record.get(key): errors.append(key)
    return dict(integrity='FAIL' if errors else 'PASS', errors=errors, scope=record.get('scope'), board='NOT_RUN')
