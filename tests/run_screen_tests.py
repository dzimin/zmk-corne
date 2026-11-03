#!/usr/bin/env python3
"""Host tests and exact framebuffer preview; no Zephyr SDK or Python packages needed."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "boards/shields/corne_dongle_display"


def png_from_pbm(source, destination, scale=8):
    pixels = source.read_bytes().split(b"\n", 2)[2]
    rows = bytearray()
    for y in range(64):
        row = bytearray()
        for x in range(128):
            value = 0 if pixels[y * 16 + x // 8] & (128 >> (x % 8)) else 255
            row.extend([value] * scale)
        for _ in range(scale):
            rows.append(0)
            rows.extend(row)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 128 * scale, 64 * scale, 8, 0, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="corne-screen-") as temp:
        exe = Path(temp) / "test_screen"
        pbm = Path(temp) / "screen.pbm"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-g", "-I", str(UI),
            str(ROOT / "tests/test_screen.c"), str(UI / "screen_model.c"),
            str(UI / "font_5x7.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(pbm)], check=True)
        if args.preview:
            png_from_pbm(pbm, args.preview)
            print(f"Exact 128x64 framebuffer enlarged 8x: {args.preview}")
