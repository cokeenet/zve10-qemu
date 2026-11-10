# agent 调用方法（实测可用，2026-10-28）

> 目标：在 ZV-E10 上获得 root shell 通道，此后所有操作走 TCP，不再需要 Pico / fastboot。
> 通道实现：`/log/agent_v14.so`（18,104 B），注入后 fork+setsid 成独立 daemon，监听 `0.0.0.0:7777`。

---

## 一、前提条件

```
① 相机处于【正常开机】状态（老菜单可用、WiFi 已连上）
   —— 因为 Jiritsu 注入需要 app 在跑（大容量模式 054c:0d95）
② 电脑与相机在同一网段
   当前常用: 电脑 <IFACE> = 10.42.0.1/24（热点 TestHotspot / PSK <REDACTED>）
             相机      = <CAMERA_IP> (DHCP)
③ busybox 的 applet 很多被裁掉了（od/head/tail/tr/id/find -size 等），
   一律用 /bin/busybox <applet> 形式调用，或改用 md5sum/ls/cat/dd/cp/mv/rm/mkdir
```

## 二、注入（一次性，插 USB 线时做）

```bash
cd /root/fw/oss/tools/payload
source ./env.sh                       # 设置 A6700_SRC

# 插上普通 USB 数据线，确认相机进了大容量模式
lsusb | grep 054c:0d95

python3 send_payload.py vendor/agent_v14.so --pid 0x0D95
```

**成功标志（必须看到这三行）：**
```
first Senser authentication: OK
second Senser authentication: OK
SET_FILE2       code=0 body=00000000  status=0x00000000 (OK)
```

**然后拔掉 USB 线**，等 10~30 秒让 WiFi 回来（插着 USB 时相机切大容量模式、WiFi 是断的）。

## 三、验证通道

```bash
cd /root/fw/oss/tools/payload

python3 cam.py ping                      # 期望: PONG
python3 cam.py run "uname -a"
python3 cam.py --ip <CAMERA_IP> ping     # IP 不同时显式指定
```

**找不到 IP 时：**
```bash
ip neigh show dev <IFACE> | grep -v FAILED
for i in $(seq 2 254); do
  (timeout 0.2 bash -c "echo > /dev/tcp/10.42.0.$i/7777" 2>/dev/null && echo "★ 10.42.0.$i") &
done; wait
```

## 四、cam.py 四种操作

```bash
# 1) 执行命令（root 权限，sh -c）
python3 cam.py run "ls -la /log"
python3 cam.py run "/bin/busybox df /usr /tmp /log 2>&1"
python3 cam.py run "cat /proc/mounts | /bin/busybox grep usr"

# 2) 取文件（二进制安全）
python3 cam.py pull /log/zve10_agent.log --out agent.log
python3 cam.py pull /dev/nflasha15                     # 打到 stdout（注意巨大）

# 3) 推文件（二进制安全）
python3 cam.py push /tmp/hello.txt --file ./hello.txt
python3 cam.py push /log/autonet.so --file autonet.so

# 4) 存活检查
python3 cam.py ping
```

## 五、协议原文（自己写客户端时用）

```
长度前缀，全部小端无关、纯 ASCII 头 + 原始字节体：

  PING\n                              → "PONG\n"

  RUN <n>\n  + n 字节命令             → "OUT <m>\n" + m 字节输出
                                      → "ERR ...\n"（失败）

  PULL <n>\n + n 字节路径             → "DATA <m>\n" + m 字节文件内容
                                      → "ERR ...\n"

  PUSH <n>\n + n 字节(path\0data)     → "OK\n"
                                      → "ERR create" / "ERR ..." 

  注意: PUSH 的 n 是 "路径长度 + 1 + 数据长度"，路径与数据之间用一个 \0 分隔
```

**Python 最小客户端：**
```python
import socket
s = socket.create_connection(("<CAMERA_IP>", 7777), timeout=20)

def read_line():
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1)
        if not c: raise EOFError
        b += c
    return b

def read_exact(n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b

def run(cmd):                      # 返回 str
    d = cmd.encode()
    s.sendall(b"RUN %d\n" % len(d) + d)
    h = read_line().decode().strip()
    if not h.startswith("OUT "): return "ERR:" + h
    return read_exact(int(h[4:])).decode("utf-8", "replace")

def pull(path):                    # 返回 bytes
    p = path.encode()
    s.sendall(b"PULL %d\n" % len(p) + p)
    h = read_line().decode().strip()
    if not h.startswith("DATA "): return None
    return read_exact(int(h[5:]))

def push(path, data):              # 返回 "OK" / "ERR ..."
    blob = path.encode() + b"\0" + data
    s.sendall(b"PUSH %d\n" % len(blob) + blob)
    return read_line().decode().strip()

print(run("uname -a"))
```

## 六、必须知道的坑

```
① PUSH 单次不要超过 ~4 MB
   agent 内部按 malloc(n+1) 分配，128 MB 内存机上超过 4 MB 必失败。
   ⇒ 传大文件要分块，或用 PULL 方向的反向技巧，或走 fastboot。

② PUSH 遇到【已存在】的文件可能返回 "ERR create"
   实测 open(O_WRONLY|O_CREAT|O_TRUNC) 会失败。
   ⇒ 先 rm 再 PUSH：
       python3 cam.py run "/bin/busybox rm -f /目标路径"
       python3 cam.py push /目标路径 --file ./local

③ agent 是【已经在跑】才连得上；它不会自己启动
   ⇒ 每次相机重启后都要重新注入一次 Jiritsu
   ⇒ /setting/mode/preload 的多条目写法实测不可靠，不要依赖自动启动

④ 插 USB 时 WiFi 断开 ⇒ 7777 不可达，这是正常的，不是 agent 挂了

⑤ /usr 的写入【不持久】（属主 root 也改不动闪存）
   ⇒ 用 agent 改 /usr 只会改到页缓存，重启即复原
   ⇒ 真正落盘的只有 /setting、/tmp、/log（都是 vfat 分区）

⑥ busybox 缺失大量 applet
   od / head / tail / tr / id / find -size / stat 等都没有
   ⇒ 用 /bin/busybox ls|cat|dd|md5sum|cp|mv|rm|mkdir|gzip|tar
   ⇒ 二进制取文件的正确方式：cam.py pull（不要用 run "cat" 再解码，字节会被吞成 ?）
```

## 七、排障速查

| 现象 | 原因 | 处理 |
|---|---|---|
| `ping` 超时 | WiFi 未连 / USB 还插着 | 拔 USB、确认相机 WiFi |
| `PONG` 但 `RUN` 无输出 | 命令不存在 | 换 `/bin/busybox xxx` |
| `ERR create` | 目标文件已存在 | 先 rm 再 push |
| 注入后连不上 | 相机 WiFi 没回来 | 等 30 s；或重新注入 |
| 端口通但不是相机 | 另一个设备占了 7777 | 用 `--ip` 指定 |
| 传回的文件字节变了 | 用了 `run "cat ..."` 文本通道 | 改用 `pull` |
