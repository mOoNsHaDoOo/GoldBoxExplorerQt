#!/usr/bin/env python3
"""Generate small synthetic files for smoke-testing the Qt port.

The files are not real game data; they are minimal examples of each container
so that the DAX/TLB/GEO/ECL/character parsers can be exercised without
shipping copyrighted SSI assets.
"""

from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "testdata"


def write_dax(blocks: list[tuple[int, bytes]], path: Path) -> None:
    data_offset = 2 + len(blocks) * 9
    header = struct.pack("<h", data_offset - 2)
    body = b""
    for block_id, data in blocks:
        header += struct.pack("<BiHH", block_id, len(body), 0, len(data))
        body += data
    path.write_bytes(header + body)


def palette_6bit(count: int, base: int = 0) -> bytes:
    return b"".join(
        bytes([((base + i) * 2) % 64, (i * 3) % 64, (i * 5) % 64])
        for i in range(count)
    )


def make_dax_images(out: Path) -> None:
    # Simple VGA image (8x8) and EGA image (8x8).
    h, w = 8, 1
    pal = palette_6bit(16)
    pixels = bytes([(i * 3) % 16 for i in range(h * w * 8)])
    vga = bytes([h, w, 0, 0, 0, 0, 1, 0, 0, 15]) + pal + pixels

    ega = struct.pack("<HHHHB", h, w, 0, 0, 1) + bytes(8)
    ega += bytes([((i * 5) % 16) | (((i + 3) % 16) << 4) for i in range(32)])

    write_dax([(1, vga), (2, ega)], out / "PIC.DAX")

    # Larger VGA + three-frame EGA sheet.
    h, w = 100, 40
    pal = palette_6bit(16)
    pixels = bytes([(((x // 16) + (y // 16)) % 16) for y in range(h) for x in range(w * 8)])
    vga = bytes([h, w, 0, 0, 0, 0, 1, 0, 0, 15]) + pal + pixels

    ega = struct.pack("<HHHHB", h, w, 0, 0, 3) + bytes(8)
    for item in range(1, 4):
        ega += bytes([
            (((x // 8) + item) % 16) | ((((x // 8) + 3) % 16) << 4)
            for _ in range(h)
            for x in range(0, w * 8, 2)
        ])
    write_dax([(1, vga), (2, ega)], out / "BIG.DAX")

    # VGA sprite (h=8,w=1) and EGA sprite.
    h, w = 8, 1
    pic = h * w * 8
    sprite = bytearray([h, 0, w, 0, 0, 0, 0, 0, 1, 16, 16])
    sprite += palette_6bit(16)
    sprite += bytes(183 - len(sprite))  # count*3 + 132 + 3*frames
    sprite += bytes([63]) + bytes([(i * 3) % 16 for i in range(pic)])

    ega_sprite = bytearray([1])
    ega_sprite += bytes(4) + struct.pack("<HHHH", 8, 1, 0, 0) + bytes(3) + bytes(8)
    ega_sprite += bytes([((i * 5) % 16) | (((i + 2) % 16) << 4) for i in range(8 * 4)])
    write_dax([(1, bytes(sprite)), (2, bytes(ega_sprite))], out / "SPRITES.DAX")

    # VGA strata image.
    strata = struct.pack("<HHHHB", 8, 1, 0, 0, 1) + bytes([0]) + bytes(7)
    strata += bytes([((x + y) % 16) for y in range(8) for x in range(5)])
    write_dax([(1, strata)], out / "STRATA.DAX")

    # VGA mixed, EGA variant.
    mixed = bytearray([1, 0, 0, 0, 0]) + struct.pack("<HHHHB", 8, 1, 0, 0, 1)
    mixed += bytes([0]) + bytes(7)
    mixed += bytes([((i * 3) % 16) | (((i + 5) % 16) << 4) for i in range(8 * 4)])
    write_dax([(1, bytes(mixed))], out / "MIXED.DAX")

    # VGA mixed, bit-planar variant.
    mixed = bytearray([1, 0, 0, 0, 0]) + struct.pack("<HHHHB", 8, 1, 0, 0, 1)
    mixed += bytes([1]) + bytes(8)
    mixed += bytes([0xFF] * 40)
    write_dax([(1, bytes(mixed))], out / "MIXEDVGA.DAX")

    # 8x8 monochrome font blocks.
    mono = bytes([0xAA, 0x55, 0xFF, 0x00, 0x81, 0x42, 0x24, 0x18]) * 4
    write_dax([(1, mono)], out / "8X8TEST.DAX")


def write_tile_library(path: Path, width: int, height: int) -> None:
    raw = bytes([((x // 4) + (y // 4)) % 16 for y in range(height) for x in range(width)])
    image_header = struct.pack("<HhhBB", height, 0, 0, width // 4, 16)
    image = image_header + raw

    header_size = 16
    offsets_start = header_size + 4
    image_offset = offsets_start + 4
    colour_offset = image_offset + len(image)
    colour_table = struct.pack("<HHHBB", 0, 0, 16, 0, 0)
    colour_table += b"".join(
        bytes([(i * 17) % 256, (i * 7) % 256, (i * 3) % 256]) for i in range(16)
    )
    file_size = colour_offset + len(colour_table)

    data = struct.pack("<4sIHBB4s", b"TILE", file_size, 2, 0, 0, b"TILE")
    data += struct.pack("<I", colour_offset)
    data += struct.pack("<I", image_offset)
    data += image + colour_table
    path.write_bytes(data)


def make_data_files(out: Path) -> None:
    (out / "POOL.CFG").write_bytes(b"x")
    (out / "TEXT.TXT").write_text("Hello Gold Box\nSecond line\n" + "A" * 40 + "\n")
    (out / "BINARY.BIN").write_bytes(bytes([0, 1, 2, 3, 0, 255, 7, 8]) * 20)

    game = bytearray(b"My Adventure".ljust(32, b"\0"))
    game += struct.pack("<iiii", 12345, 10, 20, 30) + bytes([1, 2, 3, 0])
    for i in range(8):
        game += f"KEY{i}".encode().ljust(16, b"\0")
    for i in range(12):
        game += f"ITEM{i}".encode().ljust(16, b"\0")
    game += b"secret".ljust(15, b"\0")
    (out / "GAME001.DAT").write_bytes(game)

    (out / "SAVGAM1.CSV").write_bytes(bytes(range(100)))

    vault = bytearray(struct.pack("<IIIhH", 100, 5, 7, 2, 0))
    for i in range(50):
        vault += bytes([i % 255, 1, 2, 3]) + bytes(14)
    (out / "VAULT1.DAT").write_bytes(vault)

    items = bytearray()
    for i in range(254):
        items += bytes([i, 0, 0, 1]) + struct.pack("<hh", i % 50, i * 2)
        items += bytes([i % 6, 0, 1, 0, 0, 1, 2, 3, 4])
    (out / "ITEM.DAT").write_bytes(items)

    details = bytearray()
    for _ in range(254):
        details += bytes([0, 1, 1, 6, 0, 1, 0, 1, 1, 1, 8, 0, 0, 0, 0, 0])
    (out / "ITEMS.DAT").write_bytes(details)

    character = bytearray(0x11D)
    character[0] = 15
    character[1:16] = b"TEST CHARACTER".ljust(15)
    character[0x10:0x16] = bytes([18, 12, 10, 14, 16, 9])
    character[0x2D] = 40
    character[0x2E] = 7
    character[0x2F] = 2
    (out / "CHAR.SAV").write_bytes(character)
    write_dax([(1, bytes(character))], out / "MON1.DAX")


def make_geo_files(out: Path) -> None:
    geo = bytearray(26)
    geo += bytes([16, 16])
    geo += bytes([1, 2, 3])
    geo += bytes([0])
    geo += bytes([4, 5, 6, 7])
    geo += bytes([8, 9])
    geo += bytes(8 * 4)
    geo += bytes(8 * 4)
    geo += bytes(8 * 4)
    geo += bytes(8)
    geo += b"TEST MODULE".ljust(16, b"\0")
    for i in range(8):
        geo += f"ZONE{i}".encode().ljust(16, b"\0")
    geo += bytes([10, 11])
    geo += bytes(8)
    geo += bytes(16)
    geo += bytes([12, 13])
    geo += bytes(8)
    for i in range(576):
        geo += bytes([1, 2, 3, 4, i % 40, (i // 40) % 16])
    geo += bytes(8)
    for _ in range(100):
        geo += bytes([0, 0, 0, 0]) + bytes(16)
    geo += bytes(4)
    geo += bytes([0])
    geo += bytes(400)
    geo += bytes(6760)
    (out / "GEO1.DAT").write_bytes(geo)

    geo_dax = bytearray(1026)
    for i in range(256):
        geo_dax[i + 2] = 0x12
        geo_dax[i + 258] = 0x34
        geo_dax[i + 514] = i % 40
        geo_dax[i + 770] = 0x55
    write_dax([(1, bytes(geo_dax))], out / "GEO1.DAX")


def make_ecl_file(out: Path) -> None:
    payload = bytes([0x80, 8]) + b"ABCDEFGH"
    write_dax([(1, payload)], out / "ECL1.DAX")


def main() -> None:
    OUT.mkdir(exist_ok=True)
    make_dax_images(OUT)
    write_tile_library(OUT / "IMAGES.TLB", 8, 8)
    write_tile_library(OUT / "BIG.TLB", 32, 32)
    make_data_files(OUT)
    make_geo_files(OUT)
    make_ecl_file(OUT)
    print(f"Wrote synthetic test files to {OUT}")


if __name__ == "__main__":
    main()
