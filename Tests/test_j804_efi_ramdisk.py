# SPDX-License-Identifier: MIT
"""Host test of `Tools/j804-winpe-ramdisk.py build-efi`: an appended RAM disk
around any UEFI application, as the J804 windows FD boots it. Synthetic
input only (a stand-in PE file); needs mtools."""

import hashlib
import importlib.util
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "Tools" / "j804-winpe-ramdisk.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("j804_winpe_ramdisk", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@unittest.skipUnless(shutil.which("mformat") and shutil.which("mcopy"), "needs mtools")
class BuildEfi(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="j804-efi-rd-"))
        self.app = self.tmp / "app.efi"
        self.app.write_bytes(b"MZ" + bytes(range(256)) * 64)
        self.extra = self.tmp / "extra.bin"
        self.extra.write_bytes(b"\xa5" * 5000)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def build(self, name, *more):
        cmd = [sys.executable, "-I", str(TOOL), "build-efi", "--efi", str(self.app), "--name", name,
               "--out-dir", str(self.tmp / "out"), "--label", "PROBE", *more]
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_layout_header_and_reproducibility(self):
        a = self.build("a", "--file", f"sub/extra.bin={self.extra}")
        self.assertEqual(a.returncode, 0, a.stderr)
        b = self.build("b", "--file", f"sub/extra.bin={self.extra}")
        self.assertEqual(b.returncode, 0, b.stderr)
        out = self.tmp / "out"
        img_a, img_b = (out / "a.img").read_bytes(), (out / "b.img").read_bytes()
        self.assertEqual(hashlib.sha256(img_a).digest(), hashlib.sha256(img_b).digest())
        tool = load_tool()
        info = tool.check_image(out / "a.img", out / "hdr-a.bin")
        self.assertEqual(info["image_size"], len(img_a))
        self.assertEqual(info["partition_lbas"][0], 2048)
        hdr = (out / "hdr-a.bin").read_bytes()
        self.assertEqual(hdr[:8], b"NTASIRDK")
        self.assertEqual(struct.unpack_from("<IIQ", hdr, 8), (2, 0x1000, len(img_a)))
        listing = subprocess.run(["mtype", "-i", f"{out / 'a.img'}@@{2048 * 512}", "::/EFI/BOOT/BOOTAA64.EFI"],
                                 capture_output=True, env={"MTOOLS_SKIP_CHECK": "1", "PATH": "/usr/bin:/bin:/opt/homebrew/bin"})
        self.assertEqual(listing.stdout, self.app.read_bytes())

    def test_refusals(self):
        self.app.write_bytes(b"\x7fELF")
        r = self.build("x")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not a PE image", r.stdout + r.stderr)
        self.app.write_bytes(b"MZ")
        r = self.build("y", "--size-mib", "1", "--file", f"big={self.extra}")
        self.assertNotEqual(r.returncode, 0)
        r = self.build("z", "--label", "bad label!")
        self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()
