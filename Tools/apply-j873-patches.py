#!/usr/bin/env python3
"""Apply the two pinned J873 Mu dependency fixes without changing gitlinks.

Refuse a different dependency revision, conflicting edits, or a partial patch.
Check every dependency before modifying any. Preserve already applied fixes.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
patches = (
    ('Common/MU', 'ba613eea328e801d9582059a8e0af414097a7fa8', 'mu-plus-ebs.patch'),
    ('Silicon/ARM/TIANO', '7b3db1e6fe68d5d456512a88c217ec1dcde31e3b',
     'arm-stack-alignment.patch'),
)
pending = []
for directory, expected, filename in patches:
    repo = root / directory
    patch = root / 'Tools/j873-patches' / filename
    actual = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'],
                                     text=True).strip()
    if actual != expected:
        raise SystemExit(f'{directory}: expected pinned dependency {expected}, got {actual}')
    command = ['git', '-C', str(repo), 'apply', '--ignore-space-change']
    reverse = subprocess.run(command + ['--reverse', '--check', str(patch)],
                             capture_output=True)
    if reverse.returncode == 0:
        print(f'{directory}: J873 patch already applied')
        continue
    subprocess.run(command + ['--check', str(patch)], check=True)
    pending.append((directory, command, patch))
for directory, command, patch in pending:
    subprocess.run(command + [str(patch)], check=True)
    print(f'{directory}: applied J873 patch')
