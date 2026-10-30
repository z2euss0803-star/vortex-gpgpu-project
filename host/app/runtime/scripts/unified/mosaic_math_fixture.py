"""Use original weights for small real-C math propagation, not formal quality."""
import hashlib,json,pathlib,subprocess,sys
from scripts.mosaic.assets import load_assets,write_c_manifest
from tests.mosaic_s8.contract_fixture import write
def main():
 out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
 env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text())
 asset=pathlib.Path(env['mosaic_assets']);manifest_path=asset.parent/'manifest.json';manifest=json.loads(manifest_path.read_text());raw=asset.read_bytes();load_assets(raw,manifest)
 write_c_manifest(manifest,out/'mosaic_fixture.h');write(out/'contract')
 receipt=dict(scope='ORIGINAL_W16A16_WEIGHTS_SMALL_SYNTHETIC_INPUT_NOT_FORMAL21',asset=str(asset),asset_sha256=hashlib.sha256(raw).hexdigest(),manifest_sha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),gpu_raw_cycles=None)
 (out/'fixture.json').write_bytes((json.dumps(receipt,indent=2)+'\n').encode())
if __name__=='__main__':main()
