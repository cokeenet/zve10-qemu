#!/usr/bin/env python3
"""Bounded, read-only camera preload/ssboot probe. Never restarts or writes camera files."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

COMMAND = r'''BB=/bin/busybox
echo PROBE_BEGIN
for f in /proc/sys/kernel/random/boot_id /proc/uptime /proc/ssboot/state /proc/ssboot/mode /proc/ssboot/resmode /sys/power/image_mode /sys/power/image_dev /etc/ld.so.preload; do
 echo "PATH=$f"
 $BB cat "$f" 2>&1
done
for pid in 1 149 157; do
 echo "PID=$pid"
 $BB cat /proc/$pid/comm 2>&1
 $BB cat /proc/$pid/stat 2>&1
 $BB grep -E 'libnewmenu|zve10_agent' /proc/$pid/maps 2>&1
done
echo NEW_UTILITY_MAPS
$BB grep -E 'libnewmenu|zve10_agent' /proc/self/maps 2>&1
echo PROBE_END
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip', required=True)
    parser.add_argument('--port', type=int, default=7777)
    parser.add_argument('--timeout', type=float, default=6)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    client_path = Path(__file__).resolve().parents[2] / 'payload' / 'agent' / 'cam.py'
    spec = importlib.util.spec_from_file_location('camera_client', client_path)
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    with client.conn(args.ip, args.port, args.timeout) as connection:
        body, header = client.cmd_run(connection, COMMAND)
    if body is None:
        raise RuntimeError(header)
    print(json.dumps({
        'kind': 'camera-readonly-snapshot-probe',
        'ip': args.ip,
        'port': args.port,
        'command': COMMAND,
        'response_sha256': hashlib.sha256(body).hexdigest(),
        'response': body.decode('utf-8', errors='replace'),
        'limitations': [
            'PIDs 1, 149 and 157 are candidates from prior observations; inspect returned comm before attributing them.',
            'Process starttime is creation time, not latest exec time.',
            'Kernel status does not identify which particular userspace processes were restored.'
        ],
    }, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
