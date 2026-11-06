#!/usr/bin/env python3
"""Probe several FileControl paths in ONE service session.

Each connect() attempt consumes the camera's mass-storage mode; after the
session the camera usually needs a physical USB replug. So gather as much as
possible per attempt.

Usage:
  sudo ZVE10_TOOLKIT=... python3 -B probe_paths.py
"""
import sys
import pathlib

TOOLKIT = pathlib.Path(__import__('os').environ['ZVE10_TOOLKIT']) / 'linux'
sys.path.insert(0, str(TOOLKIT / 'host'))
sys.path.insert(0, str(TOOLKIT / 'vendor' / 'Sony-PMCA-RE'))

import zve10_common as c  # noqa: E402

PATHS = [
    '/version.txt',
    '/MODE.DAT',
    '/setting/version.txt',
    '/dev/nflashaB0',          # 4 MB block node, documented as readable
    '/bin/busybox',            # documented as readable
]


def probe(camera):
    print('CONNECTED camera object: %r' % (camera,), flush=True)
    for path in PATHS:
        try:
            resp, data = c.read_path(camera, path)
            n = len(data) if data is not None else -1
            head = data[:48] if data else b''
            print('PATH %-24s response=%-5r len=%-8d head=%r'
                  % (path, resp, n, head), flush=True)
        except Exception as exc:                      # noqa: BLE001
            print('PATH %-24s EXC %s' % (path, exc), flush=True)
    print('PROBE_DONE', flush=True)


if __name__ == '__main__':
    c.connect(probe)
