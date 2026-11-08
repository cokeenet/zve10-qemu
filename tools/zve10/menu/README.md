# 真机新菜单入口（裁剪版）

从 QEMU 原型 `libnativeentry.so` 裁剪而来，只保留真机运行必需的部分。

## 相对原版的差异

| 项 | 原版（QEMU） | 本版（真机） |
|---|---|---|
| 大小 | 214,236 B | **130,676 B** |
| 观察者 hook | 29 个 | **2 个**（字体接口） |
| native hook | 4 个 | 4 个 |
| headless 帧导出 | 有 | **无** |
| UART transport | 有 | **无** |
| `record()` 日志 | 88 处调用 | 限流 40 次写 stderr |
| 依赖 | QEMU 观察者框架 | **仅 glibc** |

## 保留的功能

```
✓ 原「设置5 / Setup5」页新增 "New Menu" 行
✓ ↑↓ 可移出/移回
✓ ENTER 进入 A7C2 原资源菜单（44 条 descriptor / 16 图标 / 原文案）
✓ MENU 返回原页，焦点恢复
```

## 安全设计

1. **每个 hook 前校验原函数前 12 字节**，不匹配就静默退出，不改原厂代码
2. **版本锚点**：`CmnViewSettingNode` 构造函数偏移必须是 `0xb8eb21`
3. **`dl_iterate_phdr` 等库加载**，不假设加载顺序
4. **失败只影响新菜单**，原厂 UI 照常
5. **不写任何持久存储**

## 关键地址（绑定固件版本）

```
libObj.so          0x626538   字串按 ID 取（换标题）
                   0x6266d0   字串按字符指针取
viewUnified2.so    0x3014f0   populate
                   0x2fccc0   main
                   0x862130   row
CautionConfig.so   0xb8dffa   init
```

**校验依据**：`libObj.so` sha256 = `cfdc59b9...`，`viewUnified2.so` = `bdf0690f...`，
与本机固件一致（见 `build.json` 的 `base_members`）。

## 编译

```sh
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib
$TC/bin/arm-buildroot-linux-gnueabihf-gcc -shared -fPIC -O2 -std=gnu11 -Wall \
  -I. -o libnewmenu.so mini_main.c menu_shell.c -ldl -lpthread -lrt
```

## 部署

```sh
# 放到相机 /log
/log/libnewmenu.so

# preload 里加上（顺序：先 agent 后 newmenu，newmenu 需要在 UI 库加载后工作）
/setting/mode/preload =
  /usr/tool/libefence.so:/log/agent_v14.so:/log/libnewmenu.so
```

## 未验证

- 真机行为（只在 QEMU 里由原作者验收过）
- 物理按键映射
- 新菜单里各分类的实际内容（原版是空菜单）
