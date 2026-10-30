import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import hashlib
import zlib
from PIL import Image

spec=importlib.util.spec_from_file_location('texture_sender',Path(__file__).resolve().parents[1]/'desktop/texture_sender.py')
sender=importlib.util.module_from_spec(spec)
spec.loader.exec_module(sender)

class Preprocess(unittest.TestCase):
    def test_cover_fills_landscape_portrait_small_and_extreme_aspects_without_padding(self):
        for dimensions in ((800,400),(400,800),(8,4),(4,8),(4096,1),(1,4096)):
            for size in (512,2048):
                with self.subTest(dimensions=dimensions,size=size):
                    out=sender.prepare_lod(Image.new('RGBA',dimensions,(200,30,10,255)),size)
                    self.assertEqual(out.size,(size,size))
                    self.assertEqual(out.getextrema(),((200,200),(30,30),(10,10),(255,255)))
    def test_cover_crops_center_instead_of_stretching_side_panels(self):
        src=Image.new('RGB',(1200,400),(200,30,10))
        src.paste((10,160,80),(400,0,800,400))
        out=sender.prepare_lod(src,512)
        for point in ((128,128),(384,128),(128,384),(384,384)):
            self.assertEqual(out.getpixel(point),(10,160,80,255))
    def test_exif_rotation_still_fills_texture_and_keeps_original_file(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'oriented.png';src=Image.new('RGB',(8,4),(10,40,200))
            exif=src.getexif();exif[274]=6;src.save(path,exif=exif)
            before=path.read_bytes();prepared=sender.prepare_image(path)
            self.assertEqual(prepared.source_size,(8,4))
            self.assertEqual(prepared.oriented_size,(4,8))
            self.assertEqual(prepared.source_orientation,6)
            self.assertEqual(prepared.preview.getextrema(),((10,10),(40,40),(200,200),(255,255)))
            self.assertEqual(path.read_bytes(),before)
    def test_transparent_regions_take_visible_color_without_artificial_white(self):
        src=Image.new('RGBA',(8,8),(255,255,255,0))
        src.paste((20,60,100,255),(2,2,6,6))
        for size in (512,2048):
            out=sender.prepare_lod(src,size)
            self.assertEqual(out.getextrema(),((20,20),(60,60),(100,100),(255,255)))
    def test_partially_transparent_visible_pixels_use_deterministic_visible_mean(self):
        src=Image.new('RGBA',(512,512),(20,60,100,128));src.paste((60,100,140,128),(256,0,512,512))
        out=sender.prepare_lod(src,512)
        self.assertEqual(out.getpixel((0,256)),(30,70,110,255))
        self.assertEqual(out.getpixel((511,256)),(50,90,130,255))
    def test_full_transparency_uses_neutral_gray_fallback(self):
        out=sender.prepare_lod(Image.new('RGBA',(2,2),(255,255,255,0)),512)
        self.assertEqual(out.getextrema(),((128,128),(128,128),(128,128),(255,255)))
    def test_real_white_artwork_is_preserved(self):
        src=Image.new('RGB',(512,512),(255,255,255));src.paste((20,60,100),(128,128,384,384))
        out=sender.prepare_lod(src,512)
        self.assertEqual(out.getpixel((0,0)),(255,255,255,255))
        self.assertEqual(out.getpixel((256,256)),(20,60,100,255))
    def test_cover_upscales_small_image_and_keeps_opaque_alpha(self):
        src=Image.new('RGBA',(8,4),(200,30,10,255))
        out=sender.prepare_lod(src,512)
        self.assertEqual(out.size,(512,512))
        self.assertEqual(out.getpixel((0,0)),(200,30,10,255))
        self.assertEqual(out.getpixel((256,256)),(200,30,10,255))
        self.assertEqual(out.getpixel((256,127)),(200,30,10,255))
        self.assertEqual(out.getpixel((256,128)),(200,30,10,255))
    def test_transparent_png_composites_neutral_gray_instead_of_black(self):
        src=Image.new('RGBA',(2,2),(0,0,0,0))
        out=sender.prepare_lod(src,2048)
        self.assertEqual(out.size,(2048,2048))
        self.assertEqual(out.getpixel((1024,1024)),(128,128,128,255))
    def test_original_file_remains_unchanged_and_two_lods_have_full_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'source.png';Image.new('RGB',(9,3),(10,40,200)).save(path)
            before=path.read_bytes();prepared=sender.prepare_image(path)
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual(prepared.source_size,(9,3))
            self.assertEqual(tuple(map(len,prepared.lods)),(1048576,16777216))
            self.assertEqual(prepared.lods[0][:4],bytes((10,40,200,255)))
    def test_receipt_binds_immutable_source_lods_and_peer_report_without_visual_claim(self):
        report=Path(__file__).resolve().parents[3]/'reports/v23-warrior-adaptiveglaze-20261012'
        with tempfile.TemporaryDirectory(prefix='protocol-receipt-test-',dir=report) as d:
            source=Path(d)/'source.png';Image.new('RGB',(9,3),(10,40,200)).save(source)
            before=source.read_bytes();prepared=sender.prepare_image(source)
            applied=dict(state=3,session=123,tx_id=99,small_crc=zlib.crc32(prepared.lods[0]),large_crc=zlib.crc32(prepared.lods[1]),rgba_crc=1,depth_crc=2,asset_id=2,pose=25,gpu_jobs=12,peer_ip='127.0.0.1',peer_port=5007,wire_hex='00'*64,received_at_utc='2026-10-11T14:00:00Z')
            path=sender.save_receipt(prepared,applied,Path(d)/'receipt.json');r=json.loads(Path(path).read_text(encoding='utf8'))
            self.assertEqual(r['source']['sha256'],hashlib.sha256(before).hexdigest())
            self.assertEqual(r['source']['oriented_size'],[9,3])
            self.assertEqual(r['lods'][1]['bytes'],16777216)
            self.assertEqual(r['lods'][1]['sha256'],hashlib.sha256(prepared.lods[1]).hexdigest())
            self.assertEqual(r['raw_applied'],applied)
            self.assertIn('preprocessing',r)
            self.assertEqual(r['preprocessing']['policy'],'cover-visible-mean-v1')
            self.assertEqual(r['preprocessing']['geometry'],'cover')
            self.assertEqual(r['preprocessing']['crop'],'center')
            self.assertEqual(r['preprocessing']['background_rgb'],[10,40,200])
            self.assertEqual(r['evidence_scope'],'peer_reported_udp_applied_not_visual')
            self.assertFalse(r['visual_acceptance'])
            self.assertFalse(r['physical_board_proof'])
            self.assertEqual(source.read_bytes(),before)

if __name__=='__main__':unittest.main()
