#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the J804 M3 WinPE RAM disk from Windows ARM64 media.

Output, for each variant:
  WINPE-<variant>.img  a GPT disk with one FAT32 Basic Data partition that holds
                       EFI/BOOT/BOOTAA64.EFI, EFI/Microsoft/Boot (BCD, fonts,
                       resources), boot/boot.sdi and sources/boot.wim
  hdr-<variant>.bin    the 4 KiB NTASIRDK v2 header for that image
                       (Silicon/Apple/AppleSiliconPkg/Include/AppendedRamdisk.h)

The J804 launch appends both to the 0x1E00000-byte FD:
  run_guest.py -r -E 0 -a hdr-<variant>.bin -a WINPE-<variant>.img J804WIN_EFI.fd
so the header lands at FD base + PcdFdSize, where T8152 MemoryInitPeiLib looks
for it, and BootRamdiskHelperDxe registers the image with RamDiskDxe and sets
BootNext to \\EFI\\BOOT\\BOOTAA64.EFI on the GPT partition whose unique GUID is
4E544153-492D-4742-9414-000000000002.

Nothing from the media is stored in this repository: the tool reads the
caller's own copy (a mounted ISO) and writes only to --out-dir.

The GPT layout follows the UEFI specification (2.10, section 5.3); the FAT32
volume is made by mtools (mformat/mmd/mcopy); boot.wim is exported and edited
with wimlib-imagex (one index, LZX at level 100: still plain LZX chunks, which
the Windows boot manager reads, about 3% smaller than the media's LZX).
Standard library only.

usage:
  j804-winpe-ramdisk.py build --media DIR --out-dir DIR [--index 1]
                              [--variant stay] [--variant reboot]
  j804-winpe-ramdisk.py build-efi --efi APP.efi --name NAME --out-dir DIR
                              [--file DST=SRC ...] [--label LABEL] [--size-mib 8]
  j804-winpe-ramdisk.py check --image WINPE.img --header hdr.bin

build-efi makes the same GPT + one FAT Basic Data partition layout (and the
same header) around any UEFI application, as \\EFI\\BOOT\\BOOTAA64.EFI, so
that the windows FD boots it straight from the appended RAM disk (or, as a
tiny AHCI disk, from --windows-ahci-boot disk). Small images use FAT12/16.
The output is byte-reproducible: mtools runs with SOURCE_DATE_EPOCH (the
application's own mtime is not used) and TZ=UTC, the serial is fixed.
"""

import argparse
import hashlib
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib

SECTOR = 512
MIB = 1 << 20
CLUSTER = 4096

# AppendedRamdisk.h
HDR_SIGNATURE = b"NTASIRDK"
HDR_VERSION = 2
HDR_SIZE = 0x1000
HDR_MAX_IMAGE = 0x40000000

# BootRamdiskHelperDxe.c mNtasiEvidencePartitionGuid: the unique partition GUID
# of partition 1 that the BootNext option is built for.
PART_GUID = uuid.UUID("4e544153-492d-4742-9414-000000000002")
# Fixed, non-identifying disk GUID in the same NTAS namespace.
DISK_GUID = uuid.UUID("4e544153-492d-4742-9414-000000000001")
# UEFI spec: Microsoft Basic Data partition type.
BASIC_DATA = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128
PART_START_LBA = 2048
VOLUME_LABEL = "M6WINPE"
VOLUME_SERIAL = "4d364d33"  # "M6M3"

# Boot-manager fonts kept: the Latin ones an en-US boot uses. The CJK fonts
# (13 MiB) are left out to keep the upload small.
KEEP_FONTS = ("wgl4_boot.ttf", "segmono_boot.ttf", "segoe_slboot.ttf",
              "segoen_slboot.ttf")

VARIANTS = ("stay", "reboot")


def die(msg):
    sys.exit(f"j804-winpe-ramdisk: {msg}")


def run(cmd, log, extra_env=None, **kw):
    log.append(" ".join(cmd))
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1", MTOOLS_NO_VFAT="0", **(extra_env or {}))
    r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    if r.returncode:
        die(f"{cmd[0]} failed ({r.returncode}):\n{r.stdout.decode(errors='replace')[-2000:]}")
    return r.stdout.decode(errors="replace")


def find_ci(root, rel):
    """Case-insensitive path lookup inside the mounted media."""
    cur = pathlib.Path(root)
    for part in rel.split("/"):
        hits = [p for p in cur.iterdir() if p.name.lower() == part.lower()]
        if len(hits) != 1:
            die(f"{rel} not found in the media")
        cur = hits[0]
    return cur


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(8 * MIB), b""):
            h.update(blk)
    return h.hexdigest()


def crc32_file(path, length):
    crc = 0
    with open(path, "rb") as f:
        left = length
        while left:
            blk = f.read(min(8 * MIB, left))
            if not blk:
                die("short read during CRC")
            crc = zlib.crc32(blk, crc)
            left -= len(blk)
    return crc & 0xFFFFFFFF


# ---------------------------------------------------------------- WinPE script

def m6pe_cmd(variant):
    """The console script winpeshl starts. Plain cmd built-ins and WinPE
    System32 tools only (no WMIC, no PowerShell). Every line goes to the
    console and to X:\\m6pe.log."""
    tail_stay = r"""
call :mark "M6-WINPE-READY variant stay: cmd.exe stays open. Log X:\m6pe.log. Reset: wpeutil reboot"
exit /b 0
"""
    tail_reboot = r"""
call :mark "M6-WINPE-REBOOT variant reboot: wpeutil reboot in about 30 s. Log X:\m6pe.log"
ping -n 31 127.0.0.1 >nul 2>&1
call :say "M6-WINPE-REBOOT now"
wpeutil reboot
call :say "M6-WINPE-REBOOT wpeutil reboot returned %ERRORLEVEL%"
exit /b 0
"""
    body = r"""@echo off
setlocal EnableExtensions
color 1F
title M6 WinPE M3 (@VARIANT@)
set "M6LOG=X:\m6pe.log"
set "M6TMP=X:\m6pe.tmp"
echo M6 WinPE M3 log, variant @VARIANT@> "%M6LOG%"

call :mark "M6-WINPE-MARKER 1 winpeshl started m6pe.cmd, variant @VARIANT@"
call :run ver
call :say "M6-WINPE-ENV arch=%PROCESSOR_ARCHITECTURE% cpus=%NUMBER_OF_PROCESSORS% systemroot=%SystemRoot% date=%DATE% time=%TIME%"
set M6CPUS=0
for /f "tokens=6 delims=\" %%a in ('reg query HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor 2^>nul') do if not "%%a"=="" set /a M6CPUS+=1
call :say "M6-WINPE-CPUS registry=%M6CPUS% env=%NUMBER_OF_PROCESSORS%"
call :run reg query HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0
call :run reg query HKLM\HARDWARE\DESCRIPTION\System\BIOS

call :mark "M6-WINPE-MARKER 2 starting wpeinit"
call :run wpeinit
call :mark "M6-WINPE-MARKER 3 wpeinit returned"
call :run wpeutil UpdateBootInfo
call :run reg query HKLM\SYSTEM\CurrentControlSet\Control /v PEFirmwareType
call :run reg query HKLM\SYSTEM\CurrentControlSet\Control /v PEBootType
call :run reg query HKLM\SYSTEM\CurrentControlSet\Control /v SystemStartOptions
call :quiet mountvol
> X:\m6dp.txt echo list disk
>> X:\m6dp.txt echo list volume
call :run diskpart /s X:\m6dp.txt
call :quiet pnputil /enum-devices /connected
call :quiet ipconfig /all
call :mark "M6-WINPE-MARKER DONE cpus=%NUMBER_OF_PROCESSORS% registry=%M6CPUS%"
echo Summary (every M6-WINPE line of the log):
find "M6-WINPE" "%M6LOG%"
@TAIL@
:mark
echo.
echo ==================================================================
echo %~1
echo ==================================================================
>> "%M6LOG%" echo %~1
exit /b 0

:say
echo %~1
>> "%M6LOG%" echo %~1
exit /b 0

:run
echo ^> %*
>> "%M6LOG%" echo ^> %*
%* > "%M6TMP%" 2>&1
set M6RC=%ERRORLEVEL%
type "%M6TMP%"
type "%M6TMP%" >> "%M6LOG%"
echo [rc=%M6RC%]
>> "%M6LOG%" echo [rc=%M6RC%]
exit /b 0

:quiet
echo ^> %* (output only in the log)
>> "%M6LOG%" echo ^> %*
%* >> "%M6LOG%" 2>&1
set M6RC=%ERRORLEVEL%
echo [rc=%M6RC%]
>> "%M6LOG%" echo [rc=%M6RC%]
exit /b 0
"""
    tail = tail_stay if variant == "stay" else tail_reboot
    text = body.replace("@TAIL@", tail.strip("\n")).replace("@VARIANT@", variant)
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii")


WINPESHL_INI = (
    "[LaunchApps]\r\n"
    "%SYSTEMROOT%\\System32\\cmd.exe, /k %SYSTEMROOT%\\System32\\m6pe.cmd\r\n"
).encode("ascii")


# ---------------------------------------------------------------- GPT

def gpt_entry(type_guid, part_guid, first, last, name):
    n = name.encode("utf-16-le")[:72].ljust(72, b"\0")
    return type_guid.bytes_le + part_guid.bytes_le + struct.pack("<QQQ", first, last, 0) + n


def gpt_header(current, backup, first_usable, last_usable, entry_lba, entries_crc):
    hdr = struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x00010000, 92, 0, 0,
                      current, backup, first_usable, last_usable, DISK_GUID.bytes_le,
                      entry_lba, GPT_ENTRIES, GPT_ENTRY_SIZE, entries_crc)
    crc = zlib.crc32(hdr) & 0xFFFFFFFF
    hdr = hdr[:16] + struct.pack("<I", crc) + hdr[20:]
    return hdr.ljust(SECTOR, b"\0")


def write_gpt(path, total_lbas, part_first, part_last, part_name=VOLUME_LABEL):
    entries = gpt_entry(BASIC_DATA, PART_GUID, part_first, part_last, part_name)
    entries = entries.ljust(GPT_ENTRIES * GPT_ENTRY_SIZE, b"\0")
    ecrc = zlib.crc32(entries) & 0xFFFFFFFF
    entry_sectors = len(entries) // SECTOR
    first_usable = 2 + entry_sectors
    last_usable = total_lbas - 2 - entry_sectors
    backup_entry_lba = total_lbas - 1 - entry_sectors
    # Protective MBR (UEFI 2.10 5.2.3): one 0xEE partition covering the disk.
    mbr = bytearray(SECTOR)
    size = min(total_lbas - 1, 0xFFFFFFFF)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff", 1, size)
    mbr[510:512] = b"\x55\xaa"
    with open(path, "r+b") as f:
        f.seek(0)
        f.write(mbr)
        f.write(gpt_header(1, total_lbas - 1, first_usable, last_usable, 2, ecrc))
        f.write(entries)
        f.seek(backup_entry_lba * SECTOR)
        f.write(entries)
        f.write(gpt_header(total_lbas - 1, 1, first_usable, last_usable, backup_entry_lba, ecrc))


# ---------------------------------------------------------------- checks

def check_image(image, header):
    """Mirror of NtasiValidateAppendedRamdisk, NtasiValidateGptFatDisk and
    NtasiValidateFatVolume (AppendedRamdisk.h), plus the partition-number and
    unique-GUID match FindRamdiskPartitionDevicePath needs."""
    hdr = pathlib.Path(header).read_bytes()
    if len(hdr) != HDR_SIZE:
        die(f"header is {len(hdr)} bytes, not {HDR_SIZE}")
    sig, ver, hsize, isize, icrc, hcrc = struct.unpack_from("<8sIIQII", hdr)
    if sig != HDR_SIGNATURE or ver != HDR_VERSION or hsize != HDR_SIZE:
        die("bad header signature, version or size")
    if zlib.crc32(hdr[:28]) & 0xFFFFFFFF != hcrc:
        die("bad header CRC")
    if any(hdr[32:]):
        die("header padding is not zero")
    size = os.path.getsize(image)
    if not 512 <= isize <= HDR_MAX_IMAGE or isize != size:
        die(f"header ImageSize {isize:#x} vs file {size:#x} (limit {HDR_MAX_IMAGE:#x})")
    if crc32_file(image, size) != icrc:
        die("image CRC mismatch")
    with open(image, "rb") as f:
        head = f.read(PART_START_LBA * SECTOR)
        if head[510:512] != b"\x55\xaa" or sum(head[446 + i * 16 + 4] == 0xEE for i in range(4)) != 1:
            die("protective MBR")
        g = head[SECTOR:2 * SECTOR]
        (magic, rev, hsz, hcrc2, _r, cur, bak, fu, lu, _dg, elba, ecount, esize,
         ecrc) = struct.unpack_from("<8sIIIIQQQQ16sQIII", g)
        if magic != b"EFI PART" or rev != 0x00010000 or not 92 <= hsz <= 512 or cur != 1:
            die("GPT header")
        if zlib.crc32(g[:16] + b"\0\0\0\0" + g[20:hsz]) & 0xFFFFFFFF != hcrc2:
            die("GPT header CRC")
        total = size // SECTOR
        if size % SECTOR or bak != total - 1 or not 2 <= fu <= lu < bak or not 2 <= elba < fu:
            die("GPT geometry")
        ents = head[elba * SECTOR:elba * SECTOR + ecount * esize]
        if zlib.crc32(ents) & 0xFFFFFFFF != ecrc:
            die("GPT entries CRC")
        datas = []
        for i in range(ecount):
            e = ents[i * esize:(i + 1) * esize]
            if e[:16] == BASIC_DATA.bytes_le:
                first, last = struct.unpack_from("<QQ", e, 32)
                if not fu <= first <= last <= lu:
                    die("partition outside the usable range")
                datas.append((i + 1, e[16:32], first, last))
        if len(datas) != 1:
            die(f"{len(datas)} Basic Data partitions, need exactly 1")
        num, pguid, first, last = datas[0]
        if num != 1 or pguid != PART_GUID.bytes_le:
            die("partition 1 does not carry the NTAS unique GUID")
        f.seek(first * SECTOR)
        bs = f.read(SECTOR)
        f.seek(bak * SECTOR)
        backup = f.read(SECTOR)
    bps, spc, rsv, nfat = struct.unpack_from("<HBHB", bs, 11)
    ts = struct.unpack_from("<H", bs, 19)[0] or struct.unpack_from("<I", bs, 32)[0]
    psize = (last - first + 1) * SECTOR
    if not (bs[0] in (0xE9, 0xEB) and bs[510:512] == b"\x55\xaa" and 512 <= bps <= 4096 and
            bps & (bps - 1) == 0 and spc and spc & (spc - 1) == 0 and rsv and 1 <= nfat <= 4 and
            ts * bps == psize):
        die("FAT boot sector does not pass NtasiValidateFatVolume")
    if backup[:8] != b"EFI PART":
        die("backup GPT header missing")
    return {"image_size": size, "image_crc32": icrc, "header_crc32": hcrc,
            "reservation": (HDR_SIZE + size + 0xFFF) & ~0xFFF,
            "partition_lbas": (first, last), "fat_bytes_per_sector": bps,
            "fat_sectors_per_cluster": spc}


# ---------------------------------------------------------------- build

def wim_info(wim, index, log):
    out = run(["wimlib-imagex", "info", str(wim), str(index)], log)
    info = {}
    for line in out.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            info.setdefault(k.strip(), v.strip())
    return info


def build_variant(variant, media, base_wim, out_dir, work, log, mtime):
    wim = work / f"boot-{variant}.wim"
    scripts = work / f"scripts-{variant}"
    scripts.mkdir()
    (scripts / "m6pe.cmd").write_bytes(m6pe_cmd(variant))
    (scripts / "winpeshl.ini").write_bytes(WINPESHL_INI)
    shutil.copyfile(base_wim, wim)
    cmds = (f"add '{scripts / 'winpeshl.ini'}' /Windows/System32/winpeshl.ini\n"
            f"add '{scripts / 'm6pe.cmd'}' /Windows/System32/m6pe.cmd\n")
    log.append(f"(update commands: add winpeshl.ini and m6pe.cmd into /Windows/System32)")
    run(["wimlib-imagex", "update", str(wim), "1", "--rebuild", "--no-acls"], log,
        input=cmds.encode())
    # Pin the WIM's mtime only: `mcopy -m` carries it into the FAT directory entry, so it
    # is part of the image contents.  The output files themselves keep their real mtimes.
    os.utime(wim, (mtime, mtime))

    boot = find_ci(media, "efi/microsoft/boot")
    files = [
        ("EFI/BOOT/BOOTAA64.EFI", find_ci(media, "efi/boot/bootaa64.efi")),
        ("EFI/Microsoft/Boot/BCD", find_ci(media, "efi/microsoft/boot/bcd")),
        ("EFI/Microsoft/Boot/boot.stl", find_ci(media, "efi/microsoft/boot/boot.stl")),
        ("EFI/Microsoft/Boot/winsipolicy.p7b", find_ci(media, "efi/microsoft/boot/winsipolicy.p7b")),
        ("boot/boot.sdi", find_ci(media, "boot/boot.sdi")),
        ("sources/boot.wim", wim),
    ]
    for sub in ("resources", "cipolicies"):
        d = find_ci(boot, sub)
        for p in sorted(d.rglob("*")):
            if p.is_file():
                files.append((f"EFI/Microsoft/Boot/{sub}/{p.relative_to(d).as_posix()}", p))
    fonts = find_ci(boot, "fonts")
    for name in KEEP_FONTS:
        files.append((f"EFI/Microsoft/Boot/Fonts/{name}", find_ci(fonts, name)))

    dirs = sorted({str(pathlib.PurePosixPath(dst).parents[i])
                   for dst, _ in files for i in range(len(pathlib.PurePosixPath(dst).parents) - 1)},
                  key=lambda s: (s.count("/"), s))
    data = sum((p.stat().st_size + CLUSTER - 1) // CLUSTER * CLUSTER for _, p in files)
    data += (len(dirs) + 1) * CLUSTER * 4
    clusters = data // CLUSTER
    fat_bytes = 2 * 4 * (clusters + 2)
    part_bytes = data + fat_bytes + 32 * SECTOR + 4 * MIB
    part_bytes = (part_bytes + MIB - 1) // MIB * MIB
    total_bytes = PART_START_LBA * SECTOR + part_bytes + MIB
    if total_bytes > HDR_MAX_IMAGE:
        die(f"image would be {total_bytes:#x} bytes, above the 1 GiB ABI limit")
    part_first = PART_START_LBA
    part_last = part_first + part_bytes // SECTOR - 1

    img = out_dir / f"WINPE-{variant}.img"
    with open(img, "wb") as f:
        f.truncate(total_bytes)
    spec = f"{img}@@{part_first * SECTOR}"
    run(["mformat", "-i", spec, "-F", "-T", str(part_bytes // SECTOR), "-c", str(CLUSTER // SECTOR),
         "-H", str(part_first), "-N", VOLUME_SERIAL, "-v", VOLUME_LABEL, "::"], log)
    for d in dirs:
        run(["mmd", "-i", spec, f"::/{d}"], log)
    for dst, src in files:
        run(["mcopy", "-i", spec, "-m", "-o", str(src), f"::/{dst}"], log)
    write_gpt(img, total_bytes // SECTOR, part_first, part_last)
    log.append(f"(GPT written: partition 1 LBA {part_first}-{part_last}, type Basic Data, "
               f"unique GUID {str(PART_GUID).upper()})")
    free = run(["minfo", "-i", spec, "::"], log)

    size = img.stat().st_size
    crc = crc32_file(img, size)
    hdr = struct.pack("<8sIIQI", HDR_SIGNATURE, HDR_VERSION, HDR_SIZE, size, crc)
    hdr += struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF)
    hdr_path = out_dir / f"hdr-{variant}.bin"
    hdr_path.write_bytes(hdr.ljust(HDR_SIZE, b"\0"))
    chk = check_image(img, hdr_path)
    listing = run(["mdir", "-i", spec, "-/", "::"], log)
    return {"variant": variant, "wim": wim, "image": img, "header": hdr_path, "check": chk,
            "files": files, "listing": listing, "minfo": free,
            "script_sha256": hashlib.sha256(m6pe_cmd(variant)).hexdigest()}


def cmd_build(a):
    media = pathlib.Path(a.media)
    out_dir = pathlib.Path(a.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    variants = a.variant or list(VARIANTS)
    for t in ("wimlib-imagex", "mformat", "mmd", "mcopy", "minfo", "mdir"):
        if not shutil.which(t):
            die(f"{t} not found")
    log = []
    src_wim = find_ci(media, "sources/boot.wim")
    info = wim_info(src_wim, a.index, log)
    if info.get("Edition ID") != "WindowsPE" or info.get("Architecture") != "ARM64":
        die(f"index {a.index} is not an ARM64 WinPE image: {info.get('Name')}")
    if "Setup" in info.get("Name", "") and not a.allow_setup:
        die("this index is the Setup image; use the plain WinPE index (normally 1)")
    mtime = int(find_ci(media, "efi/boot/bootaa64.efi").stat().st_mtime)
    work = pathlib.Path(tempfile.mkdtemp(prefix="j804-winpe-", dir=a.work_dir))
    a.work_actual = work
    try:
        base = work / "base.wim"
        run(["wimlib-imagex", "export", str(src_wim), str(a.index), str(base),
             "--compress=LZX:100", "--recompress", "--boot"], log)
        results = [build_variant(v, media, base, out_dir, work, log, mtime) for v in variants]
        inputs = {
            "EFI/BOOT/BOOTAA64.EFI": find_ci(media, "efi/boot/bootaa64.efi"),
            "EFI/Microsoft/Boot/BCD": find_ci(media, "efi/microsoft/boot/bcd"),
            "boot/boot.sdi": find_ci(media, "boot/boot.sdi"),
            "sources/boot.wim": src_wim,
        }
        write_manifest(out_dir, a, info, inputs, results, log)
    finally:
        if not a.keep_work:
            shutil.rmtree(work)
    for r in results:
        print(f"{r['image'].name}: {r['check']['image_size']} bytes, sha256 {sha256(r['image'])}")
        print(f"{r['header'].name}: sha256 {sha256(r['header'])}")


def write_manifest(out_dir, a, info, inputs, results, log):
    L = []
    L.append("J804 M3 WinPE RAM disk manifest (j804-winpe-ramdisk.py)")
    L.append("Microsoft files: built from the owner's own media; never commit any of these outputs.")
    L.append("")
    L.append(f"media label: {a.media_label or '(not given)'}")
    L.append(f"source WIM index {a.index}: {info.get('Name')} | edition {info.get('Edition ID')} | "
             f"arch {info.get('Architecture')} | build {info.get('Build')}.{info.get('Service Pack Build')} | "
             f"modified {info.get('Last Modification Time')}")
    L.append("")
    L.append("inputs (sha256, bytes):")
    for k, p in inputs.items():
        L.append(f"  {sha256(p)}  {p.stat().st_size:>11}  media:{k}")
    L.append("")
    for r in results:
        c = r["check"]
        L.append(f"variant {r['variant']}:")
        L.append(f"  {sha256(r['image'])}  {c['image_size']:>11}  {r['image'].name}")
        L.append(f"  {sha256(r['header'])}  {HDR_SIZE:>11}  {r['header'].name}")
        L.append(f"  boot.wim (inside the image) sha256 {sha256(r['wim'])} bytes {r['wim'].stat().st_size}")
        L.append(f"  m6pe.cmd sha256 {r['script_sha256']}")
        L.append(f"  header: NTASIRDK v2, ImageSize {c['image_size']:#x}, ImageCrc32 {c['image_crc32']:#010x}, "
                 f"HeaderCrc32 {c['header_crc32']:#010x}; Mu reservation {c['reservation']:#x}")
        L.append(f"  GPT partition 1 LBA {c['partition_lbas'][0]}-{c['partition_lbas'][1]}, "
                 f"Basic Data, unique GUID {str(PART_GUID).upper()}, FAT32 "
                 f"{c['fat_bytes_per_sector']} B/sector x {c['fat_sectors_per_cluster']}")
        L.append("  host check (mirror of AppendedRamdisk.h validators): PASS")
        L.append("  files:")
        for dst, src in r["files"]:
            L.append(f"    {src.stat().st_size:>11}  {dst}")
        L.append("")
    L.append("commands run (paths shortened to names):")
    subs = [(str(a.work_actual), "<work>"), (str(out_dir.resolve()), "<out>"), (str(out_dir), "<out>"),
            (str(pathlib.Path(a.media).resolve()), "<media>"), (str(pathlib.Path(a.media)), "<media>")]
    for c in log:
        for old, new in subs:
            c = c.replace(old, new)
        L.append("  " + c)
    (out_dir / "MANIFEST.txt").write_text("\n".join(L) + "\n")


# ---------------------------------------------------------------- build-efi

EFI_EPOCH = "1735689600"  # 2025-01-01 00:00:00 UTC: every FAT time stamp
EFI_SERIAL = "4e544145"   # "NTAE"


def cmd_build_efi(a):
    out_dir = pathlib.Path(a.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for t in ("mformat", "mmd", "mcopy", "mdir"):
        if not shutil.which(t):
            die(f"{t} not found")
    files = [("EFI/BOOT/BOOTAA64.EFI", pathlib.Path(a.efi))]
    for spec in a.file or []:
        if "=" not in spec:
            die(f"--file {spec!r}: use DST=SRC")
        dst, src = spec.split("=", 1)
        files.append((dst.strip("/").replace("\\", "/"), pathlib.Path(src)))
    for dst, src in files:
        if not src.is_file():
            die(f"{src} is not a file")
    with open(files[0][1], "rb") as f:
        if f.read(2) != b"MZ":
            die(f"{files[0][1]} is not a PE image")
    label = a.label.upper()
    if not 1 <= len(label) <= 11 or not all(c.isalnum() or c in "_-" for c in label):
        die("--label: 1-11 characters, A-Z 0-9 _ -")
    part_bytes = a.size_mib * MIB
    data = sum((p.stat().st_size + CLUSTER - 1) // CLUSTER * CLUSTER for _, p in files)
    if data + MIB > part_bytes:
        die(f"{data} bytes of files do not fit a {a.size_mib} MiB partition")
    total_bytes = PART_START_LBA * SECTOR + part_bytes + MIB
    part_first = PART_START_LBA
    part_last = part_first + part_bytes // SECTOR - 1
    env = {"SOURCE_DATE_EPOCH": EFI_EPOCH, "TZ": "UTC"}
    log = []
    img = out_dir / f"{a.name}.img"
    with open(img, "wb") as f:
        f.truncate(total_bytes)
    spec = f"{img}@@{part_first * SECTOR}"
    run(["mformat", "-i", spec, "-T", str(part_bytes // SECTOR), "-H", str(part_first),
         "-N", EFI_SERIAL, "-v", label, "::"], log, env)
    dirs = sorted({str(pathlib.PurePosixPath(dst).parents[i])
                   for dst, _ in files for i in range(len(pathlib.PurePosixPath(dst).parents) - 1)},
                  key=lambda s: (s.count("/"), s))
    for d in dirs:
        run(["mmd", "-i", spec, f"::/{d}"], log, env)
    for dst, src in files:
        run(["mcopy", "-i", spec, "-o", str(src), f"::/{dst}"], log, env)
    write_gpt(img, total_bytes // SECTOR, part_first, part_last, label)
    size = img.stat().st_size
    crc = crc32_file(img, size)
    hdr = struct.pack("<8sIIQI", HDR_SIGNATURE, HDR_VERSION, HDR_SIZE, size, crc)
    hdr += struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF)
    hdr_path = out_dir / f"hdr-{a.name}.bin"
    hdr_path.write_bytes(hdr.ljust(HDR_SIZE, b"\0"))
    chk = check_image(img, hdr_path)
    listing = run(["mdir", "-i", spec, "-/", "::"], log, env)
    L = [f"J804 EFI RAM disk {a.name} (j804-winpe-ramdisk.py build-efi)", ""]
    L.append(f"  {sha256(img)}  {size:>9}  {img.name}")
    L.append(f"  {sha256(hdr_path)}  {HDR_SIZE:>9}  {hdr_path.name}")
    L.append(f"  header: NTASIRDK v2, ImageSize {size:#x}, ImageCrc32 {chk['image_crc32']:#010x}, "
             f"HeaderCrc32 {chk['header_crc32']:#010x}; Mu reservation {chk['reservation']:#x}")
    L.append(f"  GPT partition 1 LBA {part_first}-{part_last}, Basic Data, unique GUID "
             f"{str(PART_GUID).upper()}, label {label}")
    L.append("  host check (mirror of AppendedRamdisk.h validators): PASS")
    L.append("  files:")
    for dst, src in files:
        L.append(f"    {sha256(src)}  {src.stat().st_size:>9}  {dst}")
    L.append("")
    L.append(listing.rstrip())
    (out_dir / f"MANIFEST-{a.name}.txt").write_text("\n".join(L) + "\n")
    print(f"{img.name}: {size} bytes, sha256 {sha256(img)}")
    print(f"{hdr_path.name}: sha256 {sha256(hdr_path)}")


def cmd_check(a):
    r = check_image(a.image, a.header)
    print(f"OK {a.image}: {r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--media", required=True, help="root of the mounted Windows ARM64 ISO")
    b.add_argument("--media-label", help="ISO file name, recorded in the manifest")
    b.add_argument("--out-dir", required=True)
    b.add_argument("--work-dir", help="scratch parent (default: system temp)")
    b.add_argument("--index", type=int, default=1)
    b.add_argument("--allow-setup", action="store_true")
    b.add_argument("--variant", action="append", choices=VARIANTS)
    b.add_argument("--keep-work", action="store_true")
    b.set_defaults(func=cmd_build)
    e = sub.add_parser("build-efi")
    e.add_argument("--efi", required=True, help="the UEFI application (\\EFI\\BOOT\\BOOTAA64.EFI)")
    e.add_argument("--name", required=True, help="output names: NAME.img, hdr-NAME.bin")
    e.add_argument("--out-dir", required=True)
    e.add_argument("--file", action="append", help="extra file DST=SRC (DST inside the volume)")
    e.add_argument("--label", default="J804EFI", help="FAT volume label and GPT partition name")
    e.add_argument("--size-mib", type=int, default=8, help="FAT partition size (MiB)")
    e.set_defaults(func=cmd_build_efi)
    c = sub.add_parser("check")
    c.add_argument("--image", required=True)
    c.add_argument("--header", required=True)
    c.set_defaults(func=cmd_check)
    a = ap.parse_args()
    a.func(a)


if __name__ == "__main__":
    main()
