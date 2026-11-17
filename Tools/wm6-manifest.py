#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check a MacMini2026 (J804 M1) or MacBookPro2026 (J804 windows) FD build and
write its hand-over manifest.

Host-only. Checks:
  * FD layout: exactly 0x1E00000 bytes, arm64 Image header magic at 0x38,
    image_size at 0x10 equal to the file size, an FV signature at 0x8028.
  * No build-host paths in any PE of the build output or in the uncompressed
    FVMAIN (the FD itself is LZMA-compressed, so it is not scanned directly).
Writes MANIFEST.txt and manifest.json next to the FD:
  sha256, POSIX cksum, size, git commit and dirty state, every submodule's
  HEAD and, for a dirty one, the sha256 of its `git diff` (the j873 patches
  are applied as uncommitted submodule changes, so this pins the source the
  FD came from), the build flags the build script passed
  (--build-flag NAME=VALUE), marker strings found
  in the PE files, the FVMAIN and FVMAIN_COMPACT file GUID/type/name lists,
  the startup.nsh that the FV carries and, for the windows profile, the ACPI
  tables the build produced and the FV carries (signature, length, sha256).
Exit status is non-zero if any check fails.
"""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import uuid

FD_SIZE = 0x1E00000

# Strings that must never appear in a shipped binary.
PATH_PATTERNS = [b'/Users/', b'/private/tmp', b'/private/var', b'/Volumes/',
                 b'/home/', b'/var/folders/']

# Marker strings looked up in the PE files (ASCII and UTF-16LE).
MARKERS = {
    'common': [
        'J873-MU-005',
        'J873_PROBE_READY',
        'J873_PROBE_DONE',
        'timer reflection disabled',
    ],
    'j804-m1': [
        'J804-MU-M1-001',
        'J804_M1: untrimmed RAM top',
        'J804_M1: reserved',
        'J804_M1: chainload firmware block',
        'J804_M1_CHECKPOINT_DXE',
    ],
    'j804-windows': [
        'J804-MU-WIN-001',
        'J804_M1: untrimmed RAM top',
        'J804_M1: reserved',
        'J804_M1: chainload firmware block',
        'J804_M1: boot hand-off copies checked',
        'J804_WINDOWS: boot hand-off sources',
        'J804_M1_CHECKPOINT_DXE',
        'J804_WINDOWS_ACPI',
        'J804_PROBE_READY',
        'J804_PROBE_DONE',
        'J804 reset: EL2, restarting through WD1',
        'SmbiosInfoDxe: ADT identity',
    ],
}
# Common markers that belong to the J873 probe; not in the windows profile.
J873_ONLY = {'J873-MU-005', 'J873_PROBE_READY', 'J873_PROBE_DONE'}
# ACPI signatures the windows profile must produce, and those it must not.
ACPI_REQUIRED = {'DSDT', 'FACP', 'APIC', 'GTDT'}
# The windows profile's DBG2 (and the DSDT's COM0) exist only in a
# J804_WINDOWS_VUART=TRUE build; a manifest must not let the two FDs be confused.
VUART_FLAG = 'J804_WINDOWS_VUART'
# The D1 AHCI build (J804_WINDOWS_AHCI=TRUE): Device (\_SB.AHC0) in the DSDT,
# the raw contract marker at FD offset 0x110 ("J804AHC1", ABAR >> 12, GSIV;
# J804Windows.h) and these FVMAIN drivers. A non-AHCI FD has none of them and
# 0xFF at the marker offset.
AHCI_FLAG = 'J804_WINDOWS_AHCI'
AHCI_FD_MARKER_OFFSET = 0x110
AHCI_FD_MARKER = b'J804AHC1' + struct.pack('<II', 0xF40000000 >> 12, 990)
AHCI_FV_DRIVERS = ('NonDiscoverablePciDeviceDxe', 'SataController', 'AtaAtapiPassThruDxe',
                   'AtaBusDxe', 'J804AhciDxe')
AHCI_MARKERS = ['J804_WINDOWS_AHCI: BootNext=Boot', 'J804_WINDOWS_AHCI refused']
# DBG2's namespace string for the vUART (ASCII, NUL-terminated; ACPI 6.4 DBG2
# Device Information structure, NamespaceString).
DBG2_COM0 = b'\\_SB.COM0\0'
# AML encodings (ACPI 6.4 spec, section 20.2): ScopeOp 0x10 and DeviceOp
# 0x5B 0x82, each followed by a PkgLength (lead byte bits 7:6 = number of
# extra bytes) that covers itself and the rest of the object. iasl writes
# Scope (\_SB) at the table's root as the relative name _SB_ (no root prefix).
AML_SCOPE = re.compile(rb'\x10([\x00-\x3f]|[\x40-\x7f].|[\x80-\xbf]..|[\xc0-\xff]...)(\\?)_SB_', re.S)
AML_DEVICE = re.compile(rb'\x5b\x82([\x00-\x3f]|[\x40-\x7f].|[\x80-\xbf]..|[\xc0-\xff]...)'
                        rb'([A-Z_][A-Z0-9_]{3})', re.S)

FFS_TYPES = {
    0x01: 'RAW', 0x02: 'FREEFORM', 0x03: 'SEC', 0x04: 'PEI_CORE',
    0x05: 'DXE_CORE', 0x06: 'PEIM', 0x07: 'DRIVER', 0x08: 'COMBINED',
    0x09: 'APPLICATION', 0x0A: 'MM', 0x0B: 'FV_IMAGE', 0x0C: 'COMBINED_MM_DXE',
    0x0D: 'MM_CORE', 0x0E: 'MM_STANDALONE', 0x0F: 'MM_CORE_STANDALONE',
    0xF0: 'PAD',
}
SECTION_UI = 0x15
SECTION_RAW = 0x19


def guid_str(raw):
    return str(uuid.UUID(bytes_le=bytes(raw))).upper()


def parse_sections(data):
    sections = []
    off = 0
    while off + 4 <= len(data):
        size = data[off] | (data[off + 1] << 8) | (data[off + 2] << 16)
        stype = data[off + 3]
        hdr = 4
        if size == 0xFFFFFF:
            size = struct.unpack_from('<I', data, off + 4)[0]
            hdr = 8
        if size < hdr:
            break
        sections.append((stype, data[off + hdr:off + size]))
        off = (off + size + 3) & ~3
    return sections


def parse_fv(fv):
    if fv[0x28:0x2C] != b'_FVH':
        raise ValueError('no _FVH signature')
    fv_len = struct.unpack_from('<Q', fv, 0x20)[0]
    hdr_len = struct.unpack_from('<H', fv, 0x30)[0]
    ext_off = struct.unpack_from('<H', fv, 0x34)[0]
    off = hdr_len
    if ext_off:
        ext_size = struct.unpack_from('<I', fv, ext_off + 16)[0]
        off = ext_off + ext_size
    off = (off + 7) & ~7
    files = []
    while off + 0x18 <= min(fv_len, len(fv)):
        name = fv[off:off + 16]
        if fv[off:off + 0x18] == b'\xff' * 0x18:
            break       # free space (a pad file also has an all-0xFF name)
        ftype = fv[off + 0x12]
        attrs = fv[off + 0x13]
        size = fv[off + 0x14] | (fv[off + 0x15] << 8) | (fv[off + 0x16] << 16)
        hdr = 0x18
        if attrs & 0x01:
            size = struct.unpack_from('<Q', fv, off + 0x18)[0]
            hdr = 0x20
        if size < hdr:
            break
        body = fv[off + hdr:off + size]
        ui = ''
        raw = None
        if ftype != 0xF0:
            for stype, sdata in parse_sections(body):
                if stype == SECTION_UI:
                    ui = sdata.decode('utf-16-le', 'replace').rstrip('\0')
                elif stype == SECTION_RAW and raw is None:
                    raw = sdata
        files.append(dict(guid=guid_str(name),
                          type=FFS_TYPES.get(ftype, hex(ftype)),
                          name=ui, size=size, raw=raw))
        off = (off + size + 7) & ~7
    return files


def aml_pkg_length(raw):
    lead = raw[0]
    extra = lead >> 6
    if extra == 0:
        return lead & 0x3F
    value = lead & 0x0F
    for i in range(extra):
        value |= raw[1 + i] << (4 + 8 * i)
    return value


def aml_extents(regex, blob):
    """(name, start, end) of each object regex matches; end from its PkgLength."""
    out = []
    for m in regex.finditer(blob):
        name = m.group(2).decode()
        start = m.start(1)           # PkgLength counts from its own first byte
        out.append((name, m.start(), start + aml_pkg_length(m.group(1))))
    return out


def dsdt_has_sb_com0(dsdt):
    """True if the DSDT AML declares Device (COM0) directly in Scope (\\_SB)."""
    return dsdt_has_sb_device(dsdt, 'COM0')


def dsdt_has_sb_device(dsdt, wanted):
    """True if the DSDT AML declares Device (wanted) directly in Scope (\\_SB)."""
    found = aml_extents(AML_SCOPE, dsdt)
    devices = aml_extents(AML_DEVICE, dsdt)
    # \_SB_ anywhere, or _SB_ only at the root (not inside another object).
    scopes = [(n, s, e) for n, s, e in found
              if n == '\\' or not any(s2 < s and e <= e2 for _, s2, e2 in found + devices)]
    for name, start, end in devices:
        if name != wanted:
            continue
        in_sb = any(s <= start and end <= e for _, s, e in scopes)
        nested = any(s < start and end <= e for n, s, e in devices if (n, s) != (name, start))
        if in_sb and not nested:
            return True
    return False


def submodule_states(root):
    """Each submodule's path, HEAD and, if dirty, the sha256 of `git diff`."""
    out = []
    listing = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-s'], text=True)
    for line in listing.splitlines():
        meta, _, path = line.partition('\t')
        if not meta.startswith('160000 '):
            continue
        sub = root / path
        if not (sub / '.git').exists():
            out.append(dict(path=path, head=None, diff_sha256=None))
            continue
        head = subprocess.check_output(['git', '-C', str(sub), 'rev-parse', 'HEAD'],
                                       text=True).strip()
        diff = subprocess.check_output(['git', '-C', str(sub), 'diff', 'HEAD'])
        untracked = subprocess.check_output(['git', '-C', str(sub), 'ls-files', '--others',
                                             '--exclude-standard'], text=True).split()
        out.append(dict(path=path, head=head,
                        diff_sha256=hashlib.sha256(diff).hexdigest() if diff else None,
                        untracked=untracked))
    return out


def posix_cksum(path):
    out = subprocess.check_output(['cksum', str(path)], text=True).split()
    return int(out[0]), int(out[1])


def find_strings(blob, needle):
    return needle.encode() in blob or needle.encode('utf-16-le') in blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source-root', required=True, type=pathlib.Path)
    ap.add_argument('--build-dir', required=True, type=pathlib.Path)
    ap.add_argument('--fd', required=True, type=pathlib.Path)
    ap.add_argument('--profile', required=True)
    ap.add_argument('--out', required=True, type=pathlib.Path)
    ap.add_argument('--build-flag', action='append', default=[], metavar='NAME=VALUE',
                    help='a build define the script passed (recorded; repeatable)')
    args = ap.parse_args()
    failures = []
    flags = {}
    for item in args.build_flag:
        name, sep, value = item.partition('=')
        if not sep or not name or name in flags:
            ap.error(f'bad or repeated --build-flag {item!r}')
        flags[name] = value

    data = args.fd.read_bytes()
    if len(data) != FD_SIZE:
        failures.append(f'FD size 0x{len(data):x} != 0x{FD_SIZE:x}')
    if data[0x38:0x3C] != b'ARM\x64':
        failures.append('FD has no arm64 Image magic at 0x38')
    if struct.unpack_from('<Q', data, 0x10)[0] != len(data):
        failures.append('FD image_size at 0x10 does not match the file size')
    if data[0x8028:0x802C] != b'_FVH':
        failures.append('FD has no FV at 0x8000')
    crc, csize = posix_cksum(args.fd)

    # Every PE the build produced, plus the uncompressed FVMAIN.
    pes = sorted(set(args.build_dir.joinpath('AARCH64').rglob('*.efi')) |
                 set(args.build_dir.joinpath('AARCH64').rglob('*.dll')))
    fvmain_path = args.build_dir / 'FV' / 'FVMAIN.Fv'
    scanned = pes + [fvmain_path]
    leaks = []
    for path in scanned:
        # clang targeting windows-msvc may emit __FILE__ with backslashes;
        # folding them also folds the UTF-16 form (0x5C 0x00 -> 0x2F 0x00).
        blob = path.read_bytes().replace(b'\\', b'/')
        for pat in PATH_PATTERNS:
            if pat in blob or pat.decode().encode('utf-16-le') in blob:
                leaks.append(f'{path.relative_to(args.build_dir)}: {pat.decode()}')

    # Markers, by the module that carries them (top-level .efi copies only).
    top = sorted(args.build_dir.joinpath('AARCH64').glob('*.efi'))
    wanted = list(MARKERS['common'])
    if args.profile == 'j804-windows':
        wanted = [m for m in wanted if m not in J873_ONLY]
    if args.profile in ('j804-m1', 'j804-windows'):
        wanted += MARKERS[args.profile]
    ahci = flags.get(AHCI_FLAG, 'FALSE') if args.profile == 'j804-windows' else None
    if ahci == 'TRUE':
        wanted += AHCI_MARKERS
    markers = {}
    for marker in wanted:
        markers[marker] = [p.name for p in top if find_strings(p.read_bytes(), marker)]
    if args.profile in ('j804-m1', 'j804-windows'):
        for marker in MARKERS[args.profile] + (AHCI_MARKERS if ahci == 'TRUE' else []):
            if not markers[marker]:
                failures.append(f'marker {marker!r} not found in any PE')

    acpi = []
    if args.profile == 'j804-windows':
        # Only the platform's own tables (the FV also carries RamDiskDxe's
        # SSDT; AppleSiliconPkg's generic GTDT is built but not in the FV),
        # and only those the FV carries: a table left in the build directory
        # by an earlier build with other flags (DBG2) is not in this FD.
        fvmain_blob = fvmain_path.read_bytes()
        for path in sorted(args.build_dir.joinpath('AARCH64', 'MacBookPro2026Pkg').rglob('*')):
            if path.suffix not in ('.aml', '.acpi') or not path.is_file():
                continue
            blob = path.read_bytes()
            if len(blob) < 36 or blob not in fvmain_blob:
                continue
            acpi.append(dict(file=path.name, signature=blob[:4].decode('ascii', 'replace'),
                             length=struct.unpack_from('<I', blob, 4)[0],
                             sha256=hashlib.sha256(blob).hexdigest(), blob=blob))
            for pat in PATH_PATTERNS:
                if pat in blob:
                    leaks.append(f'{path.name}: {pat.decode()}')
        found = {t['signature'] for t in acpi}
        for sig in sorted(ACPI_REQUIRED - found):
            failures.append(f'ACPI table {sig} not built')
        vuart = flags.get(VUART_FLAG)
        if vuart not in ('TRUE', 'FALSE'):
            failures.append(f'--build-flag {VUART_FLAG}=TRUE|FALSE not given')
        elif (vuart == 'TRUE') != ('DBG2' in found):
            failures.append(f'{VUART_FLAG}={vuart} but DBG2 is '
                            + ('built' if 'DBG2' in found else 'not built'))
        # The FV-carried tables only (the list above); one DSDT, one DBG2 at most.
        dsdt = list({t['blob'] for t in acpi if t['signature'] == 'DSDT'})
        dbg2 = list({t['blob'] for t in acpi if t['signature'] == 'DBG2'})
        if len(dsdt) != 1:
            failures.append(f'{len(dsdt)} DSDT tables in FVMAIN, expected exactly 1')
        if len(dbg2) > 1:
            failures.append(f'{len(dbg2)} DBG2 tables in FVMAIN, expected at most 1')
        if vuart in ('TRUE', 'FALSE') and len(dsdt) == 1:
            com0 = dsdt_has_sb_com0(dsdt[0])
            if (vuart == 'TRUE') != com0:
                failures.append(f'{VUART_FLAG}={vuart} but the DSDT '
                                + ('declares' if com0 else 'lacks') + ' Device (\\_SB.COM0)')
        if vuart == 'TRUE' and len(dbg2) == 1 and DBG2_COM0 not in dbg2[0]:
            failures.append('DBG2 does not name \\_SB.COM0')
        if ahci not in ('TRUE', 'FALSE'):
            failures.append(f'--build-flag {AHCI_FLAG}=TRUE|FALSE expected, got {ahci!r}')
        elif len(dsdt) == 1:
            ahc0 = dsdt_has_sb_device(dsdt[0], 'AHC0')
            if (ahci == 'TRUE') != ahc0:
                failures.append(f'{AHCI_FLAG}={ahci} but the DSDT '
                                + ('declares' if ahc0 else 'lacks') + ' Device (\\_SB.AHC0)')
    if leaks:
        failures.append(f'{len(leaks)} build-host paths in PE files or ACPI tables')

    fvmain = parse_fv(fvmain_path.read_bytes())
    compact = parse_fv(data[0x8000:])
    if ahci in ('TRUE', 'FALSE'):
        marker = data[AHCI_FD_MARKER_OFFSET:AHCI_FD_MARKER_OFFSET + len(AHCI_FD_MARKER)]
        expected = AHCI_FD_MARKER if ahci == 'TRUE' else b'\xff' * len(AHCI_FD_MARKER)
        if marker != expected:
            failures.append(f'{AHCI_FLAG}={ahci} but FD offset 0x{AHCI_FD_MARKER_OFFSET:x} '
                            f'holds {marker.hex()} (expected {expected.hex()})')
        names = {f['name'] for f in fvmain}
        for driver in AHCI_FV_DRIVERS:
            if (ahci == 'TRUE') != (driver in names):
                failures.append(f'{AHCI_FLAG}={ahci} but FVMAIN '
                                + ('carries ' if driver in names else 'lacks ') + driver)
    startup = [f for f in fvmain if f['name'] == 'startup.nsh']
    startup_text = None
    if len(startup) != 1 or startup[0]['raw'] is None:
        failures.append('FVMAIN does not carry exactly one startup.nsh RAW file')
    else:
        startup_text = startup[0]['raw'].decode('utf-8', 'replace')
        if args.profile in ('j804-m1', 'j804-windows') and 'J804_MU_FIRMWARE_READY' not in startup_text:
            failures.append('startup.nsh does not print J804_MU_FIRMWARE_READY')

    root = args.source_root
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'],
                                     text=True).strip()
    status = subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'],
                                     text=True).splitlines()
    submodules = submodule_states(root)
    for sub in submodules:
        if sub['head'] is None:
            failures.append(f'submodule {sub["path"]} is not checked out')
        elif sub['untracked']:
            failures.append(f'submodule {sub["path"]} has untracked files '
                            f'({len(sub["untracked"])}); the source state is not pinned')
    for t in acpi:
        del t['blob']

    record = dict(
        schema='wm6.mu-fd-manifest.v1',
        profile=profile_name(args.profile),
        status='BUILT_NOT_HARDWARE_TESTED',
        fd=dict(name=args.fd.name, size=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
                cksum=crc, cksum_size=csize),
        source_commit=commit,
        source_status=status,
        submodules=[{k: v for k, v in s.items() if k != 'untracked'} for s in submodules],
        build_flags=flags,
        pe_files_scanned=len(scanned),
        path_leaks=leaks,
        markers=markers,
        acpi_tables=acpi,
        startup_nsh=startup_text,
        fvmain_compact=[{k: v for k, v in f.items() if k != 'raw'} for f in compact],
        fvmain=[{k: v for k, v in f.items() if k != 'raw'} for f in fvmain],
        failures=failures,
    )
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')

    lines = [
        f'profile        {record["profile"]}',
        f'status         {record["status"]} (built on the host only)',
        f'fd             {args.fd.name}',
        f'size           {len(data)} (0x{len(data):x})',
        f'sha256         {record["fd"]["sha256"]}',
        f'cksum          {crc} {csize}',
        f'source commit  {commit}',
        'source status  ' + ('; '.join(s.strip() for s in status) or 'clean'),
    ]
    for sub in submodules:
        lines.append(f'submodule      {sub["path"]} {sub["head"]} '
                     + (f'diff sha256 {sub["diff_sha256"]}' if sub['diff_sha256'] else 'clean'))
    lines += [
        'build flags    ' + (' '.join(f'{k}={v}' for k, v in flags.items()) or 'none recorded'),
        f'path check     {len(scanned)} PE/FV files scanned, '
        f'{len(leaks)} build-host paths found',
        '',
        'markers (strings in the PE files):',
    ]
    for marker, where in markers.items():
        lines.append(f'  {marker:36} {", ".join(where) if where else "NOT FOUND"}')
    if acpi:
        lines += ['', 'ACPI tables built (signature, length, sha256, file):']
        lines += [f'  {t["signature"]}  {t["length"]:6}  {t["sha256"]}  {t["file"]}' for t in acpi]
    lines += ['', 'startup.nsh (FVMAIN RAW file):']
    lines += ['  ' + line for line in (startup_text or '').splitlines()]
    lines += ['', 'FVMAIN_COMPACT files (GUID, type, UI name):']
    lines += [f'  {f["guid"]}  {f["type"]:12} {f["name"]}' for f in compact]
    lines += ['', f'FVMAIN files ({len(fvmain)}; GUID, type, UI name):']
    lines += [f'  {f["guid"]}  {f["type"]:12} {f["name"]}' for f in fvmain]
    if failures:
        lines += ['', 'FAILURES:'] + ['  ' + f for f in failures]
    (args.out / 'MANIFEST.txt').write_text('\n'.join(lines) + '\n')

    tag = {'j804-m1': 'J804_M1_BUILT',
           'j804-windows': 'J804_WINDOWS_BUILT'}.get(args.profile, 'J873_BASELINE_BUILT')
    if failures:
        for f in failures:
            print('FAIL:', f, file=sys.stderr)
        for leak in leaks[:20]:
            print('  leak:', leak, file=sys.stderr)
        sys.exit(1)
    print(tag, record['fd']['sha256'])


def profile_name(profile):
    return {'j804-m1': 'J804 M1 (J873 native + M1 changes)',
            'j804-windows': 'J804 windows profile (MacBookPro2026Pkg, EL1 under the carrier)',
            'j873-native-baseline': 'J873 native baseline (J804_M1=FALSE)'}.get(profile, profile)


if __name__ == '__main__':
    main()
