import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("make_fd", Path(__file__).resolve().parents[1]/"tools/make_fd.py")
fd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fd)


class DiskImageTests(unittest.TestCase):
    def setUp(self):
        self.ipl = bytearray(1024)
        self.ipl[:2] = b"\x60\x00"
        self.ipl[0x20:0x28] = fd.MAGIC

    def test_geometry_metadata_padding_and_checksum(self):
        payload = bytes(range(251))*9
        image = fd.build(self.ipl, payload)
        self.assertEqual(len(image), 77*2*8*1024)
        size, total = struct.unpack_from(">II", image, 0x28)
        self.assertEqual(size, 3072)
        self.assertEqual(image[1024:1024+len(payload)], payload)
        self.assertEqual(image[1024+len(payload):1024+size], bytes(size-len(payload)))
        self.assertEqual(total, fd.checksum(image[1024:1024+size]))
        damaged = bytearray(image[1024:1024+size])
        damaged[-1] ^= 1
        self.assertNotEqual(total, fd.checksum(damaged))
        self.assertEqual(image[1024+size:], bytes(len(image)-1024-size))

    def test_reject_invalid_or_overlapping_inputs(self):
        for bad in [b"", bytes(1023), bytes(1024), bytes(1025)]:
            with self.assertRaises(ValueError):
                fd.build(bad, b"data")
        for bad in [b"", bytes(fd.MAX_PAYLOAD+1)]:
            with self.assertRaises(ValueError):
                fd.build(self.ipl, bad)
        with self.assertRaises(ValueError):
            fd.checksum(b"123")

    def test_maximum_payload_and_reproducibility(self):
        data = bytes(fd.MAX_PAYLOAD)
        image = fd.build(self.ipl, data)
        self.assertEqual(image, fd.build(self.ipl, data))
        self.assertEqual(struct.unpack_from(">I", image, 0x28)[0], fd.MAX_PAYLOAD)


if __name__ == "__main__":
    unittest.main()
