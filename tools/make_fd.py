"""Build a raw 77-cylinder, two-head, eight 1024-byte sectors/track XDF.

The first sector is our own IPL; the rest contains a contiguous, sector-padded
standalone payload and zero fill. No OS, ROM, filesystem, or boot-sector donor.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SECTOR = 1024
IMAGE_SIZE = 77 * 2 * 8 * SECTOR
MAX_PAYLOAD = 0x9C00
MAGIC = b"MT68FD1\0"


def checksum(payload):
    if len(payload) % 4:
        raise ValueError("checksum input must contain complete longwords")
    return sum(word[0] for word in struct.iter_unpack(">I", payload)) & 0xFFFFFFFF


def build(ipl, payload):
    if len(ipl) != SECTOR or ipl[0] != 0x60 or ipl[0x20:0x28] != MAGIC:
        raise ValueError("invalid 1024-byte Memtest68 boot sector")
    if not payload:
        raise ValueError("empty standalone payload")
    padded = payload + bytes((-len(payload)) % SECTOR)
    if len(padded) > MAX_PAYLOAD:
        raise ValueError("standalone payload overlaps its stack reservation")
    header = bytearray(ipl)
    struct.pack_into(">II", header, 0x28, len(padded), checksum(padded))
    image = bytes(header) + padded
    return image + bytes(IMAGE_SIZE - len(image))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipl", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    payload = args.payload.read_bytes()
    image = build(args.ipl.read_bytes(), payload)
    args.output.write_bytes(image)
    size, total = struct.unpack_from(">II", image, 0x28)
    manifest = {
        "format": "XDF / raw 2HD (77x2x8x1024)",
        "image_bytes": len(image),
        "payload_bytes": len(payload),
        "loaded_bytes": size,
        "load_address": "0x00002400",
        "checksum_be32_sum": f"0x{total:08X}",
        "sha256": hashlib.sha256(image).hexdigest(),
        "contains_human68k": False,
    }
    args.output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{args.output}: {len(image)} bytes, payload {len(payload)} bytes / {size//SECTOR} sectors")


if __name__ == "__main__":
    main()
