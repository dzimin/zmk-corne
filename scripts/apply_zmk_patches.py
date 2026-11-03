#!/usr/bin/env python3
"""Apply the reviewed patch only to the pinned ZMK source, before west build."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def apply(zmk_root):
    patches = Path(__file__).resolve().parents[1] / "patches"
    manifest = json.loads((patches / "manifest.json").read_text())
    revision = subprocess.check_output(
        ["git", "-C", str(zmk_root), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != manifest["zmk_revision"]:
        raise RuntimeError(f"Unexpected ZMK revision: {revision}")
    patch = patches / manifest["patch"]
    if sha256(patch) != manifest["patch_sha256"]:
        raise RuntimeError("Patch checksum mismatch")
    target = zmk_root / manifest["target"]
    current = sha256(target)
    if current == manifest["after_sha256"]:
        print("PASS: verified ZMK patch already applied (cached workspace)")
        return
    if current != manifest["before_sha256"]:
        raise RuntimeError("ZMK source differs from both reviewed before/after versions")
    command = ["git", "-C", str(zmk_root), "apply", "--whitespace=error"]
    subprocess.run([*command, "--check", str(patch)], check=True)
    subprocess.run([*command, str(patch)], check=True)
    if sha256(target) != manifest["after_sha256"]:
        raise RuntimeError("Patched source checksum mismatch")
    print(f"PASS: ZMK {revision}, patched central.c sha256={sha256(target)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 scripts/apply_zmk_patches.py /path/to/zmk")
    apply(Path(sys.argv[1]).resolve(strict=True))
