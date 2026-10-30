"""Executed image-to-tensor policy checks; no inference or device claims."""
import hashlib, importlib.util, json, pathlib, struct, tempfile, unittest
from contextlib import contextmanager
from PIL import Image

ROOT=pathlib.Path(__file__).resolve().parents[2]
@contextmanager
def workspace():
    base=ROOT/'build/input-tool-tests';base.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=str(base)) as temp:
        try:yield temp
        finally:
            for path in pathlib.Path(temp).rglob('*'):
                if path.is_file():path.chmod(0o600)

def tool(test):
    path=ROOT/'tools/vxgpu_input.py'
    test.assertTrue(path.is_file(),'controlled image preparation tool is required')
    spec=importlib.util.spec_from_file_location('vxgpu_input_tested',str(path))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

class InputTool(unittest.TestCase):
    def test_mosaic_preserves_actual_rgb_values_and_source_bytes(self):
        module=tool(self)
        with workspace() as temp:
            root=pathlib.Path(temp);source=root/'color.png';Image.new('RGB',(4,3),(7,23,251)).save(source)
            record=module.prepare(source,'mosaic',root/'prepared','color',4,3)
            actual=(root/'prepared/color.f32').read_bytes()
            self.assertEqual(actual,struct.pack('<36f',*([7.0]*12+[23.0]*12+[251.0]*12)))
            self.assertEqual((root/'prepared/color.source').read_bytes(),source.read_bytes())
            self.assertEqual(record['parameters']['shape'],'[3,3,4]')
            self.assertEqual(record['parameters']['tensor_sha'],hashlib.sha256(actual).hexdigest())

    def test_hunet_original_threshold_fit_and_white_padding(self):
        module=tool(self)
        with workspace() as temp:
            root=pathlib.Path(temp);source=root/'black.png';Image.new('RGB',(16,8),(0,0,0)).save(source)
            record=module.prepare(source,'hunet',root/'prepared','black')
            values=struct.unpack('<49152f',(root/'prepared/black.f32').read_bytes())
            for channel in range(3):
                plane=values[channel*16384:(channel+1)*16384]
                self.assertEqual(plane[:32*128],(1.0,)*(32*128))
                self.assertEqual(plane[32*128:96*128],(-1.0,)*(64*128))
                self.assertEqual(plane[96*128:],(1.0,)*(32*128))
            policy='HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1'
            self.assertEqual(record['parameters']['preprocessing'],policy)
            self.assertEqual(record['parameters']['preprocessing_id'],hashlib.sha256(policy.encode()).hexdigest())

    def test_bad_source_and_unsafe_id_never_create_prepared_input(self):
        module=tool(self)
        with workspace() as temp:
            root=pathlib.Path(temp);source=root/'bad.png';source.write_bytes(b'not an image')
            for input_id in ('bad','../bad','.', 'a/b'):
                with self.assertRaises((ValueError,OSError)):
                    module.prepare(source,'mosaic',root/'prepared',input_id,4,3)
                self.assertFalse((root/'prepared').exists())

    def test_repeated_output_directory_cannot_replace_an_input(self):
        module=tool(self)
        with workspace() as temp:
            root=pathlib.Path(temp);source=root/'source.png';Image.new('RGB',(4,3),(1,2,3)).save(source)
            module.prepare(source,'mosaic',root/'prepared','fixed',4,3)
            before=(root/'prepared/fixed.f32').read_bytes();Image.new('RGB',(4,3),(4,5,6)).save(source)
            with self.assertRaises(FileExistsError):module.prepare(source,'mosaic',root/'prepared','fixed',4,3)
            self.assertEqual((root/'prepared/fixed.f32').read_bytes(),before)

if __name__=='__main__':unittest.main()
