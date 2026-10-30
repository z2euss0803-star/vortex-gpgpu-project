import pathlib,tempfile,unittest
from scripts.demo import image,audit_image

class DirectNetwork(unittest.TestCase):
    def test_actual_preparation_and_rootfs_audit_reject_dhcp_conflict(self):
        install=getattr(image,'install_direct_network',None)
        check=getattr(audit_image,'network_errors',None)
        self.assertTrue(callable(install),'image prepare must install its own static interface configuration')
        self.assertTrue(callable(check),'image audit must inspect actual networking payload')
        temp_root=pathlib.Path(__file__).resolve().parents[2]/'build/demo'
        temp_root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=str(temp_root)) as d:
            meta=pathlib.Path(d)
            parent=meta/'recipes-core/init-ifupdown/files/interfaces'
            parent.parent.mkdir(parents=True)
            parent.write_text('auto eth0\niface eth0 inet dhcp\n')
            install(meta)
            interfaces=parent.read_bytes()
            init=(meta/'recipes-apps/codex-firstboot/files/codex-direct-network').read_bytes()
            recipe=(meta/'recipes-core/init-ifupdown/init-ifupdown_%.bbappend').read_text()
            self.assertIn('FILESEXTRAPATHS_prepend',recipe)
            self.assertIn('file://interfaces',recipe)
            files={'etc/network/interfaces':interfaces,'etc/init.d/codex-direct-network':init}
            self.assertEqual(check(files),[])
            for broken in [b'auto eth0\niface eth0 inet dhcp\n',interfaces+b'iface eth0 inet dhcp\n',interfaces.replace(b'192.168.13.10',b'192.168.1.2'),interfaces.replace(b'255.255.255.0',b'255.0.0.0')]:
                bad=dict(files);bad['etc/network/interfaces']=broken
                self.assertTrue(check(bad))
            bad=dict(files);bad['etc/init.d/codex-direct-network']=b'#!/bin/sh\nexit 0\n'
            self.assertTrue(check(bad))
if __name__=='__main__':unittest.main()
