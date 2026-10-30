from pathlib import Path
import importlib.util
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('crypto_config',ROOT/'scripts/hunet_opt/crypto_config.py')
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class Config(unittest.TestCase):
    def test_versioned_arm64_symbols(self):
        fragment=(ROOT/'delivery/templates/opt-crypto.cfg').read_text()
        arm=(ROOT/'tests/hunet_opt/fixtures/arm64-crypto-5.4.Kconfig').read_text()
        required={'ARM64_CRYPTO','CRYPTO_SHA256_ARM64','CRYPTO_SHA2_ARM64_CE'}
        self.assertTrue(required.issubset(mod.symbols(arm)))
        mod.validate(fragment,arm+'\nconfig CRYPTO\nconfig CRYPTO_HASH\nconfig CRYPTO_SHA256\nconfig KERNEL_MODE_NEON\n')
        with self.assertRaises(ValueError):mod.validate(fragment+'CONFIG_FAKE_SHA2=y\n',arm)
        adapter=(ROOT/'driver/vxl_digest_crypto.c').read_text()
        self.assertNotIn('arm_neon.h',adapter);self.assertNotIn('vsha256h',adapter)
if __name__=='__main__':unittest.main()
