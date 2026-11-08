#!/usr/bin/env python3
"""cam.py — 通过 agent_v14 的 TCP 7777 通道操作相机（毫秒级，无需 USB）

协议（长度前缀，避免空格换行被相机截断）：
  "RUN <n>\n"  + n 字节命令           → "OUT <m>\n" + m 字节输出
  "PULL <n>\n" + n 字节路径           → "DATA <m>\n" + m 字节
  "PUSH <n>\n" + n 字节 "路径\0数据"  → "OK\n" / "ERR ...\n"
  "PING\n"                            → "PONG\n"

用法:
  ./cam.py run "id"
  ./cam.py run "/bin/busybox ls -la /log"
  ./cam.py pull /log/zve10_agent.log --out agent.log
  ./cam.py ping
"""
import argparse, os, socket, sys

def conn(ip, port, timeout=15):
    s = socket.create_connection((ip, port), timeout=timeout)
    s.settimeout(timeout)
    return s

def read_exact(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError("连接中断")
        b += c
    return b

def read_line(s):
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1)
        if not c: raise EOFError("连接中断")
        b += c
    return b

def cmd_run(s, command):
    data = command.encode() if isinstance(command, str) else command
    s.sendall(b"RUN %d\n" % len(data) + data)
    hdr = read_line(s).decode(errors="replace").strip()
    if not hdr.startswith("OUT "):
        return None, hdr
    n = int(hdr[4:])
    return read_exact(s, n), hdr

def cmd_pull(s, path):
    p = path.encode()
    s.sendall(b"PULL %d\n" % len(p) + p)
    hdr = read_line(s).decode(errors="replace").strip()
    if not hdr.startswith("DATA "):
        return None, hdr
    n = int(hdr[5:])
    return read_exact(s, n), hdr

def cmd_push(s, path, data):
    """PUSH <n>\n + "path\0data"  → "OK" / "ERR ..." """
    p = path.encode()
    blob = p + b"\0" + data
    s.sendall(b"PUSH %d\n" % len(blob) + blob)
    return read_line(s).decode(errors="replace").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=("ping","run","pull","push"))
    ap.add_argument("arg", nargs="?", default="")
    ap.add_argument("--ip", default=os.environ.get("CAM_IP", ""), help="相机 IP（或用环境变量 CAM_IP）")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--out", help="pull 时保存到本地文件")
    ap.add_argument("--file", help="push 时的本地源文件")
    a = ap.parse_args()
    if not a.ip:
        print("错误：需要 --ip 或环境变量 CAM_IP", file=sys.stderr)
        print("  例：CAM_IP=192.168.1.100 ./cam.py ping", file=sys.stderr)
        sys.exit(2)

    try:
        s = conn(a.ip, a.port)
        if a.action == "ping":
            s.sendall(b"PING\n")
            print(read_line(s).decode().strip())
        elif a.action == "run":
            body, hdr = cmd_run(s, a.arg)
            if body is None:
                print("错误:", hdr); return 1
            print(body.decode("utf-8", errors="replace"), end="")
        elif a.action == "pull":
            body, hdr = cmd_pull(s, a.arg)
            if body is None:
                print("错误:", hdr); return 1
            if a.out:
                open(a.out,"wb").write(body)
                print("已保存 %s (%d B)" % (a.out, len(body)))
            else:
                sys.stdout.buffer.write(body)
        elif a.action == "push":
            if not a.file:
                print("push 需要 --file", file=sys.stderr); return 1
            data = open(a.file, "rb").read()
            print(cmd_push(s, a.arg, data))
        s.close()
        return 0
    except Exception as e:
        print("失败:", type(e).__name__, e, file=sys.stderr)
        return 2

if __name__ == "__main__":
    sys.exit(main())
