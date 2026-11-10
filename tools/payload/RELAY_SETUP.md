# 教室里连相机的三步（笔记本当桥）

## 前提

- 笔记本上装了 tailscale 并已登录（tailnet 里叫 `test-host`，IP `<REDACTED_IP>`）
- 相机能连上笔记本的热点

## 步骤

### ① 笔记本开热点，相机连上去

```
Windows: 设置 → 网络和 Internet → 移动热点 → 打开
把相机的 WiFi 连到该热点
```

### ② 确认 agent 在跑

相机连上 WiFi 后，agent 的 7777 端口才可用。
如果相机重启过，agent 会消失，需要用 USB 重新注入。

### ③ 跑 relay

```bash
python relay.py            # 自动扫描找相机
# 或
python relay.py <CAMERA_IP>   # 已知 IP
```

**看到 `✓ relay 就绪: 0.0.0.0:7777 → <相机IP>:7777` 就成了。**

**不要关这个窗口，一直开着。**

## 我这边怎么连

```bash
python3 cam.py --host <REDACTED_IP> --port 7777 run "..."
```

## 排查

| 现象 | 原因 |
|---|---|
| `没找到相机` | 相机没连上热点 / agent 没跑 |
| `绑定 7777 失败` | 端口被占（关掉其它占用程序） |
| 连上了但没输出 | 相机 agent 忙 / 命令超长（上限 ~1022 字符） |
