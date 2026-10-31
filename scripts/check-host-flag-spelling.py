#!/usr/bin/env python3
"""Check executable/docs snapshots; negative control: --ref BEFORE_CUTOVER_SHA."""
import subprocess
import sys
old = b'--' + b'dsl'
ref = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == '--ref' else None
command = ['git', 'ls-tree', '-r', '--name-only', '-z', ref] if ref else ['git', 'ls-files', '-z']
paths = subprocess.check_output(command).split(b'\0')
found = []
for raw in paths:
    if not raw: continue
    path = raw.decode()
    data = subprocess.check_output(['git', 'show', ref + ':' + path]) if ref else open(path, 'rb').read()
    if old in data: found.append(path)
assert not found, 'retired host option remains in: ' + ', '.join(found)
print('PASS tracked public host-flag spelling gate')
