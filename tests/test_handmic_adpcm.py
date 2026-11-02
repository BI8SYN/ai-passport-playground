from pathlib import Path
import ctypes
import importlib.util
import json
import math
import random
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AdpcmTests(unittest.TestCase):
    def test_codec(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp) / 'adpcm.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'firmware/rf-link-handmic/main/hm_adpcm.c'), '-o', str(lib)], check=True)
            codec = ctypes.CDLL(str(lib))
            pcm_t, block_t = ctypes.c_int16 * 160, ctypes.c_uint8 * 84
            codec.hm_adpcm_encode.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.POINTER(ctypes.c_uint8)]
            codec.hm_adpcm_decode.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(ctypes.c_int16)]
            codec.hm_adpcm_decode.restype = ctypes.c_bool
            block, decoded = block_t(), pcm_t()
            codec.hm_adpcm_encode(pcm_t(), block)
            self.assertEqual(bytes(block), bytes(84))
            self.assertTrue(codec.hm_adpcm_decode(block, 84, decoded))
            self.assertEqual(list(decoded), [0] * 160)
            # Fixed nibble order/index adaptation: 1 then 7 at initial step 7.
            block[4] = 0x71
            self.assertTrue(codec.hm_adpcm_decode(block, 84, decoded))
            self.assertEqual(list(decoded)[:3], [0, 1, 12])
            for offset, bad in [(2, 89), (3, 1), (83, 0x10)]:
                block = block_t()
                block[offset] = bad
                self.assertFalse(codec.hm_adpcm_decode(block, 84, decoded))
            self.assertFalse(codec.hm_adpcm_decode(block_t(), 83, decoded))
            for samples in ([32767] * 160, [-32768] * 160,
                            [int(12000 * math.sin(i * 2 * math.pi * 440 / 8000)) for i in range(160)]):
                block = block_t()
                codec.hm_adpcm_encode(pcm_t(*samples), block)
                self.assertEqual(block[3], 0)
                self.assertEqual(block[83] & 0xf0, 0)
                self.assertTrue(codec.hm_adpcm_decode(block, 84, decoded))
                self.assertEqual(decoded[0], samples[0])
                rms = math.sqrt(sum((a-b)**2 for a,b in zip(samples, decoded)) / 160)
                self.assertLess(rms, 1600)
            spec = importlib.util.spec_from_file_location('adpcm_reference', ROOT / 'tools/handmic-simulator/adpcm.py')
            reference = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(reference)
            vectors = json.loads((ROOT / 'docs/spec/rf-link-handmic/adpcm-vectors.json').read_text())
            packet = bytes.fromhex(vectors['audio_packet_hex'])
            self.assertEqual(len(packet), 100)
            self.assertEqual(packet[:4], bytes([1,1,2,160]))
            self.assertEqual(struct.unpack_from('<III',packet,4), (0x12345678,1,0))
            self.assertEqual(packet[16:], bytes.fromhex(vectors['blocks'][1]['block_hex']))
            for vector in vectors['blocks']:
                encoded = bytes.fromhex(vector['block_hex'])
                expected = bytes.fromhex(vector['decoded_pcm_hex'])
                self.assertTrue(codec.hm_adpcm_decode(block_t.from_buffer_copy(encoded), 84, decoded))
                self.assertEqual(bytes(decoded), expected)
                self.assertEqual(reference.decode(encoded), expected)
                if 'input_pcm_hex' in vector:
                    pcm = bytes.fromhex(vector['input_pcm_hex'])
                    codec.hm_adpcm_encode(pcm_t.from_buffer_copy(pcm), block)
                    self.assertEqual(bytes(block), encoded)
                    self.assertEqual(reference.encode(pcm), encoded)
            rng = random.Random(20261102)
            for _ in range(100):
                samples = [rng.randint(-32768,32767) for _ in range(160)]
                pcm = struct.pack('<160h', *samples)
                codec.hm_adpcm_encode(pcm_t(*samples), block)
                self.assertEqual(bytes(block), reference.encode(pcm))
                self.assertTrue(codec.hm_adpcm_decode(block, 84, decoded))
                self.assertEqual(bytes(decoded), reference.decode(bytes(block)))
