from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import make_hds as hd


class HardDiskImageTests(unittest.TestCase):
    def setUp(self):
        self.ipl = bytearray(1024)
        self.ipl[:2] = b"\x60\x00"
        self.ipl[0x20:0x28] = hd.MAGIC

    def test_scsi_header_ipl_payload_and_checksum(self):
        payload = bytes(range(251))*7
        image = hd.build(self.ipl, payload, 1)
        self.assertEqual(len(image), 1024*1024)
        self.assertEqual(image[:8], b"X68SCSI1")
        self.assertEqual(struct.unpack_from(">HIH", image, 8), (512, 2048, 0x0100))
        self.assertEqual(image[0x400], 0x60)
        self.assertEqual(image[0x420:0x428], hd.MAGIC)
        self.assertEqual(image[0x800:0x804], b"X68K")
        self.assertEqual(struct.unpack_from(">III", image, 0x804), (32,1024,1024))
        size, total = struct.unpack_from(">II", image, 0x428)
        self.assertEqual(size, 2048)
        self.assertEqual(image[0x8000:0x8000+len(payload)], payload)
        self.assertEqual(hd.checksum(image[0x8000:0x8000+size]), total)
        self.assertEqual(image[0x8000+len(payload):], bytes(len(image)-0x8000-len(payload)))
        damaged = bytearray(image[0x8000:0x8000+size]); damaged[0] ^= 1
        self.assertNotEqual(hd.checksum(damaged), total)

    def test_reject_invalid_inputs(self):
        for bad in [b"", bytes(1023), bytes(1024), bytes(1025)]:
            with self.assertRaises(ValueError): hd.build(bad, b"data", 1)
        for bad in [b"", bytes(hd.MAX_PAYLOAD+1)]:
            with self.assertRaises(ValueError): hd.build(self.ipl, bad, 1)
        for size in [0, -1, 257, 1.5]:
            with self.assertRaises(ValueError): hd.build(self.ipl, b"data", size)

    def test_capacity_and_reproducibility(self):
        payload = bytes(hd.MAX_PAYLOAD)
        first = hd.build(self.ipl, payload, 1)
        self.assertEqual(first, hd.build(self.ipl, payload, 1))
        self.assertEqual(struct.unpack_from(">I", first, 0x428)[0], hd.MAX_PAYLOAD)


if __name__ == "__main__":
    unittest.main()
