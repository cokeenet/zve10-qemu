#!/usr/bin/env python3
"""Direct menu boot host: read MENU_FRAME, ACK it, then pull the real bytes via QMP pmemsave."""
import socket, json, sys, os, time

ser_path, qmp_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
ser.connect(ser_path)
ser.settimeout(30.0)
log = open(os.path.join(out, 'serial.log'), 'wb')
buf = b''
frame = None

def send(t):
    ser.sendall((t + '\n').encode('ascii'))

t0 = time.time()
while time.time() - t0 < 60:
    try:
        ser.settimeout(0.5)
        d = ser.recv(262144)
    except socket.timeout:
        continue
    except Exception:
        break
    if not d:
        break
    buf += d
    log.write(d); log.flush()
    while b'\n' in buf:
        line, buf = buf.split(b'\n', 1)
        s = line.decode('utf-8', 'replace')
        if s.startswith('MENU_FRAME '):
            rec = json.loads(s.split(' ', 1)[1])
            print("MENU_FRAME seq=%s fingerprint=%s segments=%d category=%s open=%s"
                  % (rec['sequence'], rec.get('fingerprint'), len(rec.get('segments') or []),
                     rec.get('category'), rec.get('open')))
            frame = rec
        elif s.startswith('MENU_BOOT'):
            print("MENU_BOOT:", s[:160])
    if frame is not None:
        # ACK immediately so the guest keeps going
        send("ACK %d" % frame['sequence'])
        break

if frame is None:
    print("FAIL: no MENU_FRAME"); sys.exit(1)

# ---- QMP ----
q = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
q.connect(qmp_path); q.settimeout(60.0)
qs = q.makefile('rwb')
g = json.loads(qs.readline())
print("QMP greeting OK")
tid = [0]
def qcall(name, args=None, tmo=90.0):
    tid[0] += 1; i = tid[0]
    qs.write((json.dumps({'execute': name, 'arguments': args or {}, 'id': i}) + '\n').encode()); qs.flush()
    end = time.time() + tmo
    while time.time() < end:
        line = qs.readline()
        if not line: raise RuntimeError('qmp closed')
        r = json.loads(line)
        if r.get('id') != i: continue
        if 'error' in r: raise RuntimeError(str(r['error']))
        return r.get('return')
    raise RuntimeError('qmp timeout ' + name)

qcall('qmp_capabilities')
qcall('stop')

segs = sorted(frame['segments'], key=lambda x: x.get('capture_offset', 0) if 'capture_offset' in x else 0)
raw = bytearray()
ok = fail = 0
for idx, sg in enumerate(segs):
    path = os.path.join(out, 'seg_%05d.bin' % idx)
    try: os.unlink(path)
    except OSError: pass
    try:
        qcall('pmemsave', {'val': sg['physical'], 'size': sg['length'], 'filename': path}, tmo=60.0)
        raw.extend(open(path, 'rb').read()[:sg['length']]); ok += 1
    except Exception as e:
        fail += 1
    try: os.unlink(path)
    except OSError: pass

print("segments ok=%d fail=%d raw=%d B" % (ok, fail, len(raw)))
if len(raw):
    open(os.path.join(out, 'frame.rgba4444'), 'wb').write(raw)
    import hashlib
    print("FRAME sha256=%s bytes=%d" % (hashlib.sha256(raw).hexdigest(), len(raw)))
send("ACK %d" % frame['sequence']) if False else None
log.close(); ser.close()
