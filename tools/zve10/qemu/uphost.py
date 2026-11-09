#!/usr/bin/env python3
"""路线 A 的捕获主机：CAPTURE/ACK 走串口，pmemsave 走 QMP。"""
import socket, json, time, os, sys, threading, queue

ser_path, qmp_path, out = sys.argv[1], sys.argv[2], sys.argv[3]

# ---- 串口 ----
ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
ser.connect(ser_path)
ser.settimeout(30.0)
print("  serial 已连接")
log = open(os.path.join(out, 'serial.log'), 'wb')
log_lock = threading.Lock()
lines_q = queue.Queue(maxsize=200000)
_ser_stop = threading.Event()

def ser_reader():
    """持续快读串口，绝不阻塞。这是关键：读慢了 guest 的 UART FIFO 会满，
    guest 卡在写串口上，QEMU 主循环随之停摆，QMP 也就不响应了。"""
    chunk_buf = b''
    while not _ser_stop.is_set():
        try:
            ser.settimeout(0.05)
            d = ser.recv(262144)
        except socket.timeout:
            continue
        except Exception:
            break
        if not d:
            break
        chunk_buf += d
        with log_lock:
            log.write(d)
        # 按行切分
        while b'\n' in chunk_buf:
            line, chunk_buf = chunk_buf.split(b'\n', 1)
            try:
                lines_q.put_nowait(line)
            except queue.Full:
                pass
    with log_lock:
        log.flush()
    try:
        lines_q.put_nowait(None)
    except queue.Full:
        pass

_reader = threading.Thread(target=ser_reader, daemon=True)
_reader.start()

def readline(timeout=30.0):
    try:
        ln = lines_q.get(timeout=timeout)
    except queue.Empty:
        return None
    return ln

def flush_log():
    with log_lock:
        try: log.flush()
        except Exception: pass

def send(text):
    ser.sendall((text + '\n').encode('ascii'))

# ---- 等 NE_READY ----
t0 = time.time()
ready = False
ui_up = False
while time.time() - t0 < 220:
    ln = readline(15.0)
    if ln is None:
        continue
    if b'UI_AFTER_SLEEP' in ln and not ui_up:
        ui_up = True
        print("  UI 启动 %.0fs" % (time.time() - t0))
    if b'NE_READY ' in ln:
        ready = True
        print("  NE_READY %.0fs" % (time.time() - t0))
        break
if not ready:
    print("  FAIL: 未见 NE_READY"); log.close(); ser.close(); sys.exit(1)
time.sleep(2)

# ---- QMP ----
q = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
q.connect(qmp_path); q.settimeout(30.0)
qbuf = b''

# ---- QMP（照上游的写法：makefile + id 字段）----
q = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
q.connect(qmp_path)
q.settimeout(60.0)
qstream = q.makefile('rwb')

g = json.loads(qstream.readline())
if 'QMP' not in g:
    print("  FAIL: 无 QMP greeting: %s" % g)
    log.close(); ser.close(); sys.exit(1)
print("  QMP greeting OK")

_ticket = [0]
def qcall(name, arguments=None, timeout_s=90.0):
    _ticket[0] += 1
    tid = _ticket[0]
    msg = {'execute': name, 'arguments': arguments or {}, 'id': tid}
    qstream.write((json.dumps(msg) + '\n').encode())
    qstream.flush()
    end = time.time() + timeout_s
    while time.time() < end:
        try:
            q.settimeout(max(0.5, end - time.time()))
        except Exception:
            pass
        line = qstream.readline()
        if not line:
            raise RuntimeError('qmp closed')
        r = json.loads(line)
        if r.get('id') != tid:
            continue            # 事件或别的响应，跳过
        if 'error' in r:
            raise RuntimeError('qmp error: %s' % r['error'])
        return r.get('return')
    raise RuntimeError('qmp timeout on %s' % name)

qcall('qmp_capabilities', timeout_s=60.0)
print("  QMP caps OK")

# ---- CAPTURE ----
ticket = 1
send("CAPTURE %d" % ticket)
print("  已发 CAPTURE %d" % ticket)

meta = state = ready_rec = None
pages = []
end = time.time() + 90
while time.time() < end and ready_rec is None:
    ln = readline(max(0.5, end - time.time()))
    if ln is None:
        continue
    if ln.startswith(b'UI_CAPTURE_META '):
        meta = json.loads(ln.split(b' ', 1)[1])
        print("  META frame=%s segments=%s" % (meta.get('frame'), meta.get('segments')))
    elif ln.startswith(b'UI_CAPTURE_PAGE '):
        pages.append(json.loads(ln.split(b' ', 1)[1]))
    elif ln.startswith(b'UI_CAPTURE_READY '):
        ready_rec = json.loads(ln.split(b' ', 1)[1])
        print("  READY %s" % ready_rec)
    elif ln.startswith(b'NE_STATE '):
        state = json.loads(ln.split(b' ', 1)[1])

if not ready_rec or not meta:
    print("  FAIL: 协议不完整")
    log.close(); ser.close(); q.close(); sys.exit(1)

# ---- stop + pmemsave（QMP）----
try:
    qcall('stop')
    st = qcall('query-status')
    print("  已暂停: %s" % st.get('status'))
except Exception as e:
    print("  stop 失败: %s" % e)

pages_sorted = sorted(pages, key=lambda x: x.get('capture_offset', 0))
raw = bytearray()
ok = fail = 0
fail_msgs = []
for idx, pg in enumerate(pages_sorted):
    path = os.path.join(out, 'seg_%05d.bin' % idx)
    if os.path.exists(path):
        try: os.unlink(path)
        except OSError: pass
    try:
        qcall('pmemsave', {'val': pg['physical'], 'size': pg['length'],
                           'filename': path}, timeout_s=60.0)
    except Exception as e:
        fail += 1
        if len(fail_msgs) < 3:
            fail_msgs.append("idx=%d %s" % (idx, e))
        continue
    want = pg['length']
    got = 0
    for _ in range(400):
        if os.path.exists(path):
            sz = os.path.getsize(path)
            if sz >= want:
                got = sz
                break
        time.sleep(0.005)
    if got >= want:
        raw.extend(open(path, 'rb').read()[:want])
        ok += 1
    else:
        fail += 1
        if len(fail_msgs) < 3:
            fail_msgs.append("idx=%d got=%d want=%d" % (idx, got, want))
    try:
        if os.path.exists(path):
            os.unlink(path)
    except OSError:
        pass
    if idx % 25 == 0:
        print("    ... %d/%d ok=%d fail=%d" % (idx, len(pages_sorted), ok, fail))

for m in fail_msgs:
    print("  失败样本: %s" % m)
print("  抓到 raw: %d B (meta 说 %s)  ok=%d fail=%d" % (len(raw), meta.get('byte_length'), ok, fail))
if raw:
    open(os.path.join(out, 'frame-001.rgba4444'), 'wb').write(bytes(raw))

try:
    qcall('cont')
except Exception:
    pass
send("ACK %d" % ticket)
print("  已发 ACK")

# ---- 按键序列：ENTER 进入 New Menu，MENU 返回父页 ----
KEYS = ["ENTER", "MENU"]
seq = 100
for name in KEYS:
    seq += 1
    send("KEY %d %s" % (seq, name))
    print("  注入 KEY %d %s" % (seq, name))
    # 等 NE_KEY_SENT
    got = False
    end = time.time() + 20
    while time.time() < end:
        ln = readline(2.0)
        if ln is None:
            continue
        if ln.startswith(b'NE_KEY_SENT '):
            print("    %s" % ln.decode(errors='replace')[:100])
            got = True
            break
    if not got:
        print("    ! 未见 NE_KEY_SENT")
    time.sleep(3.0)
    # 再抓一帧
    ticket += 1
    send("CAPTURE %d" % ticket)
    m2 = r2 = None
    pgs2 = []
    end = time.time() + 60
    while time.time() < end and r2 is None:
        ln = readline(max(0.5, end - time.time()))
        if ln is None:
            continue
        if ln.startswith(b'UI_CAPTURE_META '):
            m2 = json.loads(ln.split(b' ', 1)[1])
        elif ln.startswith(b'UI_CAPTURE_PAGE '):
            pgs2.append(json.loads(ln.split(b' ', 1)[1]))
        elif ln.startswith(b'UI_CAPTURE_READY '):
            r2 = json.loads(ln.split(b' ', 1)[1])
        elif ln.startswith(b'NE_STATE '):
            st2 = json.loads(ln.split(b' ', 1)[1])
            print("    STATE count=%s selected=%s overlay=%s node_installed=%s" % (
                st2.get('count'), st2.get('selected_index'), st2.get('overlay'), st2.get('node_installed')))
    if not r2 or not m2:
        print("    ! 第二帧协议不完整")
        continue
    try:
        qcall('stop')
    except Exception:
        pass
    raw2 = bytearray()
    ps2 = sorted(pgs2, key=lambda x: x.get('capture_offset', 0))
    ok2 = 0
    for i2, pg in enumerate(ps2):
        path2 = os.path.join(out, 'k%d_%05d.bin' % (seq, i2))
        if os.path.exists(path2):
            try: os.unlink(path2)
            except OSError: pass
        try:
            qcall('pmemsave', {'val': pg['physical'], 'size': pg['length'],
                               'filename': path2}, timeout_s=60.0)
        except Exception:
            continue
        if os.path.exists(path2) and os.path.getsize(path2) >= pg['length']:
            raw2.extend(open(path2, 'rb').read()[:pg['length']])
            ok2 += 1
        try:
            if os.path.exists(path2):
                os.unlink(path2)
        except OSError:
            pass
    try:
        qcall('cont')
    except Exception:
        pass
    print("    frame after %s: %d B ok=%d/%d" % (name, len(raw2), ok2, len(ps2)))
    if raw2:
        open(os.path.join(out, 'frame-after-%s.rgba4444' % name), 'wb').write(bytes(raw2))
    send("ACK %d" % ticket)
    time.sleep(1.0)

log.close(); ser.close()
try: qstream.close()
except Exception: pass
q.close()
