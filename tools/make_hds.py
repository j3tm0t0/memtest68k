"""Build a standalone SCSI HDS with an X68SCSI1 header and our own IPL.

512-byte physical blocks; disk IPL at byte 0x400; payload at byte 0x8000.
This dedicated diagnostic disk has no Human68k files or filesystem.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from make_fd import MAX_PAYLOAD, checksum

BLOCK = 512
IPL_OFFSET = 0x400
PAYLOAD_OFFSET = 0x8000
MAGIC = b"MT68HD1\0"


def build(ipl, payload, size_mib=16):
    if len(ipl) != 1024 or ipl[0] != 0x60 or ipl[0x20:0x28] != MAGIC:
        raise ValueError("invalid 1024-byte Memtest68 SCSI IPL")
    if not payload:
        raise ValueError("empty standalone payload")
    padded = payload + bytes((-len(payload)) % BLOCK)
    if len(padded) > MAX_PAYLOAD:
        raise ValueError("standalone payload overlaps its stack reservation")
    if not isinstance(size_mib, int) or not 1 <= size_mib <= 256:
        raise ValueError("HDS size must be 1..256 MiB")
    size = size_mib * 1024 * 1024
    image = bytearray(size)
    image[:8] = b"X68SCSI1"
    struct.pack_into(">HIH", image, 8, BLOCK, size//BLOCK, 0x0100)
    # Standard empty partition-table header, in 1024-byte logical sectors.
    image[0x800:0x804] = b"X68K"
    struct.pack_into(">III", image, 0x804, PAYLOAD_OFFSET//1024, size//1024, size//1024)
    header = bytearray(ipl)
    struct.pack_into(">II", header, 0x28, len(padded), checksum(padded))
    image[IPL_OFFSET:IPL_OFFSET+1024] = header
    image[PAYLOAD_OFFSET:PAYLOAD_OFFSET+len(padded)] = padded
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipl", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--size-mib", type=int, default=16)
    args = parser.parse_args()
    payload = args.payload.read_bytes()
    image = build(args.ipl.read_bytes(), payload, args.size_mib)
    args.output.write_bytes(image)
    size, total = struct.unpack_from(">II", image, IPL_OFFSET+0x28)
    manifest = {
        "format": "HDS / raw X68SCSI1 / SCSI", "image_bytes": len(image),
        "physical_block_bytes": BLOCK, "ipl_offset": IPL_OFFSET,
        "payload_offset": PAYLOAD_OFFSET, "payload_bytes": len(payload),
        "loaded_bytes": size, "load_address": "0x00002400",
        "checksum_be32_sum": f"0x{total:08X}",
        "sha256": hashlib.sha256(image).hexdigest(), "contains_human68k": False,
    }
    args.output.with_suffix(".json").write_text(json.dumps(manifest, indent=2)+"\n")
    print(f"{args.output}: {len(image)} bytes, payload {len(payload)} bytes / {size//BLOCK} blocks")


if __name__ == "__main__":
    main()
