import copy
import ast
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/hunet'))
from cross_audit import audit_cross, audit_kernel


class CrossAudit(unittest.TestCase):
    def test_ubuntu18_python36_subprocess_keywords(self):
        incompatible=[]
        for name in ('build_common.py','cross_check.py','kernel_check.py','image_audit.py','prepare_image.py'):
            tree=ast.parse((ROOT/'scripts/hunet'/name).read_text(encoding='utf-8'))
            for call in ast.walk(tree):
                if isinstance(call,ast.Call) and isinstance(call.func,ast.Attribute) and isinstance(call.func.value,ast.Name) and call.func.value.id=='subprocess':
                    for keyword in call.keywords:
                        if keyword.arg in ('text','capture_output'):
                            incompatible.append((name,call.lineno,keyword.arg))
        self.assertEqual(incompatible,[], 'Ubuntu18.04 Python3.6 subprocess API')

    def record(self):
        release = dict(source_commit='ab'*20, source_digest='12'*32)
        value = dict(scope='HUNET_AARCH64_CROSS', exit=0, **release,
                     compiler=dict(target='aarch64-xilinx-linux', version='GCC 9.2.0',
                                   gcc_sha256='34'*32, gxx_sha256='56'*32),
                     runtime_versions=['GLIBC_2.17', 'GLIBCXX_3.4.26'],
                     rootfs_libraries=['libc.so.6', 'libstdc++.so.6'])
        value['elfs'] = {}
        for name in ('libvortex-hunet.so', 'vxl_hunet', 'vxl_hunet_copy', 'vxl_hunet_wmma'):
            value['elfs'][name] = dict(machine='AArch64', sha256='78'*32,
                interpreter=None if name.endswith('.so') else '/lib/ld-linux-aarch64.so.1',
                needed=['libc.so.6'], required_versions=['GLIBC_2.17'],
                source_commit=release['source_commit'], source_digest=release['source_digest'])
        return release, value

    def test_wrong_arch_or_unbound_cross(self):
        release, good = self.record()
        self.assertEqual(audit_cross(good, release)['integrity'], 'PASS')
        mutations = [(('exit',), None), (('error',), 'RUNTIME_AUDIT_ABORTED'), (('source_digest',), '00'*32),
          (('compiler', 'target'), 'x86_64-linux-gnu'),
          (('compiler', 'version'), 'GCC 13.2.0'),
          (('elfs', 'vxl_hunet', 'machine'), 'Advanced Micro Devices X86-64'),
          (('elfs', 'vxl_hunet', 'interpreter'), None),
          (('elfs', 'vxl_hunet', 'needed'), ['libmissing.so']),
          (('elfs', 'vxl_hunet', 'required_versions'), ['GLIBC_9.0']),
          (('elfs', 'vxl_hunet', 'source_digest'), '00'*32)]
        for path, value in mutations:
            bad = copy.deepcopy(good); dest = bad
            for key in path[:-1]: dest = dest[key]
            dest[path[-1]] = value
            self.assertEqual(audit_cross(bad, release)['integrity'], 'FAIL', path)

    def test_wrong_kernel_or_missing_exit(self):
        release, _ = self.record()
        good = dict(scope='PRELIMINARY_KERNEL_CROSS', exit=0, **release,
            kernel_release='5.4.0-xilinx-v2020.1', vermagic='5.4.0-xilinx-v2020.1 SMP preempt mod_unload aarch64',
            arch='AArch64', compiler='GCC 9.2.0', W=1, Werror=True,
            config=['CONFIG_ZONE_DMA32=y', 'CONFIG_64BIT=y', 'CONFIG_ARM64=y'],
            config_sha256='34'*32, symvers_sha256='56'*32, module_sha256='78'*32,
            source_tree='/native/kernel-source', kernel_build='/native/kernel-build', source_bound=True)
        self.assertEqual(audit_kernel(good, release)['integrity'], 'PASS')
        for key, value in [('exit', None), ('error', 'KERNEL_INPUT_CHANGED'), ('arch', 'X86-64'), ('source_bound', False),
            ('config', ['CONFIG_ARM64=y']), ('vermagic', '6.8.0 SMP aarch64'),
            ('compiler', 'GCC 14'), ('symvers_sha256', ''), ('Werror', False)]:
            bad = dict(good); bad[key] = value
            self.assertEqual(audit_kernel(bad, release)['integrity'], 'FAIL', key)


if __name__ == '__main__': unittest.main()
