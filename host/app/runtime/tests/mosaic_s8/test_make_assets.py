import unittest,tempfile,subprocess,shutil,os
from pathlib import Path
class AssetBuild(unittest.TestCase):
    @unittest.skipUnless(shutil.which('make'),'GNU make unavailable on this host')
    def test_external_asset_header_never_regenerated(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);header=root/'mosaic_fixture.h';header.write_text('/* external frozen catalog */')
            os.utime(str(header),(1,1))
            p=subprocess.run(['make','-f','Makefile.mosaic','-n','MOSAIC_FIXTURE='+str(root),str(header)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertEqual(p.returncode,0,p.stderr.decode())
            self.assertNotIn('--test-fixture',p.stdout.decode())
            self.assertEqual(header.read_text(),'/* external frozen catalog */')
