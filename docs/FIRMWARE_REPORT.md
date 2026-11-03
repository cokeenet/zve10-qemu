# ZV-E10 固件：从官方升级包到**完全解密解包 + 可回写重打包**（2026-10-15 突破）

> 前置结论（本文件推翻）：此前判定"FDAT 是固定密钥 ECB、密钥在相机里、升级包路线已关闭"。
> **上游新工具 `fwtool.py` 让这一切反转** —— FDAT 是 **AES-CBC**，**密钥硬编码在工具里**。

---

## 一、突破口：`fwtool`

> ### ⚠️ 归属更正（2026-11-03）
> 本文件原先标题写作 `DavidBuchanan314/fwtool.py`，**归属不完整**。
> 链条其实是：**nex-hack 原创 → ma1co 维护上游 → DavidBuchanan314 为下游分支**。
> 我们实际参考的是能解析 ZV-E10 的那个版本，两个仓库都在参考集里；
> 此处不改变技术结论（AES-CBC、密钥硬编码在工具内），只更正署名。
> 详见 `docs/ACKNOWLEDGEMENTS.md` §3。

> A tool to unpack Sony camera firmware images（源自 nex-hack，`ma1co` 维护上游）

**支持列表里明确写着 ZV-E10**：

```yaml
# devices.yml
ZV-E10:
  arch: CXD90045
  model: 0x01030010
```

**密钥硬编码在 `fwtool/sony/constants.py`**：

```python
key_aes          = b'\xE3\xB0\xC4\x42\x98\xFC\x1C\x14\x9A\xFB\xF4\xC8\x99\x6F\xB9\x24'   # = SHA-256("") 前 16 字节
key_cxd90045_k0  = b'\xC1\xAA\x8F\x7C\x46\x34\x1F\xFE\xD1\x55\x89\xFC\x81\x70\xA6\xBB
                     \x59\x25\xE8\x5F\x62\x82\xD7\xF9\x5B\xA3\xFD\xF5\xD3\x03\xE0\x6B'   # 32 字节
key_cxd90045_k8  = b'\x78\x2F\xD9\x79\x56\x68\x6B\x83\x21\xEA\xC7\xAB\xC2\xEA\x68\xDC
                     \x8E\xC5\x3E\x4B\x75\xF1\x6B\x78\x51\x74\x0B\x1D\xC1\xC0\x13\xFB'
# a7iv 分支还加了 CXD90057_k0/k8（A7 IV）
```

解密映射（`fdat.py`）：

```python
('CXD90045_k0', lambda: AesCbcCrypter(key_aes, key_cxd90045_k0)),
('CXD90045_k8', lambda: AesCbcCrypter(key_aes, key_cxd90045_k8)),
```

**补充说明**：`ZX-E10 Ver.2.03` 实际用的是 **`CXD90045_k0`**（`config.yaml` 里写明）。

### 环境准备

```bash
pip install --break-system-packages pycryptodomex pyyaml
git clone https://github.com/DavidBuchanan314/fwtool.py && cd fwtool.py
git worktree add ../fwtool_a7iv origin/a7iv     # 需要 a7iv 分支才有 k0/k8 与 CXD90045_k8
```

### 解密解包

```bash
python3 fwtool.py unpack -f FirmwareData_ZVE10V203.dat -o out_zve10
```

输出（1.3 GB）：

```
out_zve10/
  config.yaml           ← crypterName: CXD90045_k0 / version: 2.03 / model: 0x1030010 / USB VID-PID 白名单
  firmware.fdat         ← 解密后的 FDAT（魔数 "UDTRFIRM"）
  firmware.tar          ← 内层【标准 tar】，可直接编辑
  updater.img           ← cramfs 升级器镜像
  firmware.tar_unpacked/
    0100_config/  mount.conf（分区表）  partconf/（分区偏移）
    0110_backup/  SynAstra / RK / CX88700_UC2.bin
    0400_updater/ dev/nflasha1 → boot/initrd.img
    0700_part_image/
        nflasha3  (30MB, /system) → av-cam.bin bonobo.bin dfe_app.bin lif_app.bin vmlinux.bin wole_app.bin
        nflasha7  (5MB,  /rootfs) → bin cert etc kmod lens lib sbin setting system usr var version.txt
        nflasha15 (249MB)         → bin codec kmod lib local sabin sbin scenario share tool wlan
        nflasha5  (36MB, /wbi)
    0800_appli/              ← 相机应用、镜头文件（lens/VX*.bin）、字体、config.spec
    updater.img_unpacked/    ← 升级脚本（见下）
```

分区表（`mount.conf`）：

```
nflasha1  none      vfat   8MB     (updater)
nflasha2  /setting  vfat  20MB
nflasha3  /system   vfat  50MB
nflasha4  none      raw   12MB
nflasha5  /wbi      wbi   60MB
nflasha6  none      vfat  16MB
nflasha7  /rootfs   raw    8MB     (Linux rootfs)
nflasha11 /log      vfat 500MB
nflasha12 /cert     vfat  40MB
```

固件内部版本：`body_version = 100.110.035`，`version.txt = 30110_06.2026060501`

## 二、改固件链路（**注意：第 ④ 步不成立，见第三节补充**）

> ⚠️ 本节的 ④⑤ 步（重新加密/回写）**在四代机型上做不到**：`AesCbcCrypter.encrypt()` 直接抛异常，
> 且 FDAT 尾部带 **RSA-2048 签名**（索尼私钥）。本节仅供参考格式理解，**不要据此尝试刷机**。

```
官方升级包 (.exe / .dmg)
   │  ① 取 FirmwareData_*.dat
   │     · macOS：7z x Update_*.dmg        （dmg 是未加密的 HFS 盘镜像，两条路逐字节一致）
   │     · Windows：跑自解包 exe 抓临时目录
   ▼
FirmwareData_*.dat (UFU 容器: 89 55 46 55 0D 0A 1A 0A + [BE u32 长度][标签][数据] + DEND=CRC32)
   │  ② fwtool unpack            → AES-CBC 解密 → firmware.tar（标准 tar）
   ▼
firmware.tar / 分区镜像
   │  ③ 【修改】直接改 tar 内容，或改镜像内的文件系统
   ▼
   │  ④ fwtool pack -c config.yaml -f firmware.tar -u updater.img -o out
   │        → 重新加密（CXD90045_k0）
   │  ⑤ ufu_tool.py repack        → 重新装进 UFU 容器并重算 CRC32
   ▼
新的 FirmwareData_*.dat → 交给官方升级程序刷入
```

**PC 端三类校验的位置与性质**（都可绕过）：

| 校验 | 位置 | 性质 |
| --- | --- | --- |
| 容器完整性 | `DEND` = CRC32 | 普通 CRC32，重算即可 |
| 机型 | UDID 白名单（VID/PID 比对，`XpStgDevGetVenIdAndProdId`） | 白名单来自包内明文 |
| 版本 | `IDS_ERR_STATUS_INVALID_VERSION`（拒绝更新到不更新的版本） | PC 端分支，可改 |

相机端还有一层自检：固件包内含 `*_sum/*.sum` 与升级脚本
（`updater.img_unpacked/bin/us_crc32sum.sh`、`filecheck.sh`、`endufp.sh`、`us_spi_firmup.sh`），
同样是 **CRC32 校验和** —— 重打包时一并重算即可。

## 三、对之前几个问题的直接回答

| 问题 | 现在可以怎么答 |
| --- | --- |
| 能提取固件吗 | ✅ **已提取**：完整 Linux 系统（内核 `vmlinux.bin`、initrd、glibc、驱动、镜头文件、全部应用） |
| "用 ff01 改相机" | 仍无证据（`ff01` 只在蓝牙遥控服务 `8000FF00` 里，是控制通道） |
| 能把新菜单移植到 ZV-E10 吗 | 菜单/UI 就是 **`lib/viewUnified2.so`(14M)、`viewUnified4.so`(3.2M) + viewUnified3/5/6/7/8.so**（8 个模块）——**从此不再是黑盒，但移植仍需面对私有中间件（`libObj.so` 21M、`CautionConfig.so` 17M、`libmpr.so` 13M）** |
| 遥控协议 | `libInfraRemote.so`(8.4M) 就是遥控基础设施（PTP/BLE/WiFi 相关的宿主实现） |
| 加密模式之谜 | 之前差分看到"重复 16 字节块"= **AES-CBC 中明文的零填充区**，不是 ECB；差分结论"无密钥流复用"是对的，密钥来自工具而非相机 |

## 三·补、★ 重大订正（2026-10-15 晚）：**能解，但打包不可逆**

> 本文件第二节曾写"完整改固件链路已闭合"——**该结论错误，特此订正**。

### 证据 1：工具代码明确不支持加密

```python
class AesCbcCrypter(AesCrypter):
    """Decrypts a block from a 4th gen firmware image using AES CBC"""
    def decrypt(self, file):
        file.seek(-0x110, 2); size = file.tell()   # 末尾 0x110 字节在加密区之外
        self._iv = file.read(0x10)
        ...
    def encrypt(self, file):
        raise Exception('Encryption not supported')     # ← 四代直接不支持打包
```

对比：`ShaCrypter`(1代)/`AesCrypter`(2代)/`DoubleAesCrypter`(3代) **都实现了 `encryptBlock`**，
只有 **`AesCbcCrypter`（四代 = CXD90045 / CXD90057）没有**。

### 证据 2：FDAT 布局里有 256 字节签名

```
FDAT 大小 372,454,672 = 363,725 × 1024（AES-CBC 密文块） + 272
                                                         └ 256 字节签名 + 16 字节 IV
块结构: [u16 校验和][u16 大小|结束标志][1020 字节数据]
第一块明文头: checksum=0x8d6e sizeAndEndFlag=0x03fc(size=1020) 数据 = "UDTRFIRM" ...
```

### 证据 3：固件里就带着验证公钥

| 文件 | 类型 |
| --- | --- |
| `0400_updater/.../initrd.img_unpacked/etc/VerificationKey.pem` | **RSA-2048 公钥** |
| `nflasha15_unpacked/share/data/pub.key` | RSA-2048 |
| `nflasha15_unpacked/share/data/kikilog_pub.pem` | RSA-3072 |

**256 字节 = 2048 bit，与 `VerificationKey.pem` 位数一致** ⇒ FDAT 尾部就是 **RSA-2048 签名**，
由索尼私钥生成、相机/升级器用公钥验证。**没有私钥 ⇒ 无法造出合法升级包。**

### 证据 4：三代分区确实是明文（不要用 M4 工具）

```
nflasha7            : Linux rev 1.0 ext2 filesystem data
nflasha3/initrd.img : Linux rev 1.0 ext2 filesystem
（另有 vfat / cramfs，全部直接可读）
```

A7 IV 仓库的 `parse_emmc_partitions.py` 做的是 **bootrom 派生密钥的 AES-XTS**；
把它套到三代明文分区上 = 二次变换成垃圾，反过来写回则等于"给明文加一次密"。

### 结论对照

| 说法 | 判定 |
| --- | --- |
| 三代/四代没法打包，打包没密钥 | ✅ 成立（RSA 签名 + encrypt 未实现） |
| 三代机底层是明文 | ✅ 成立（ext2/vfat/cramfs） |
| 不要用 M4 工具写三代分区 | ✅ 成立（会多一层错误变换） |
| ZV-1F 与 ZV-E10 同方案，固定 A/B 未变 | ✅ 成立（`key_aes` + `key_cxd90045_*`） |
| 基本不可逆 | ✅ **仅指升级包路线**；硬件直写明文分区不受影响 |

## 三·补二、服务模式机制（固件实测，2026-10-15 深夜）

### 六个启动模式，靠 `/setting/updater/` 下的标志文件切换

`common.src` 里的官方定义：

```sh
MODE_USER=0  MODE_PROD=1  MODE_SERVICE=2  MODE_MINOR=3  MODE_MSCRYPT=5  MODE_VERSKIP=6
```

`UdtrMain.sh` 的分派逻辑：

```sh
getCurrentMode()
{
	test -f /root/IAMLDUDWRT        && return $MODE_PROD     # 串口 boot（工厂下载写入）
	test -f /setting/updater/mode3  && return $MODE_MINOR
	test -f /setting/updater/mode2  && return $MODE_SERVICE  # ★ Service mode
	test -f /setting/updater/mode6  && return $MODE_VERSKIP  # ★ Version Skip
	test -f /setting/updater/mode1  && return $MODE_PROD
	test -f /setting/updater/mode5  && return $MODE_MSCRYPT
	return $MODE_USER
}
```

`up.sh`（相机 `/bin` 下的 shell 脚本）是**模式切换器**：

```sh
"user") func_usermode  ;;  "serv") func_servicemode ;;  "ms") func_mscrypt ;;
"ver" ) func_verskip   ;;                                       # ← 免版本校验

func_verskip() { rm -f $UPDIR/mode*; touch $UPDIR/mode $UPDIR/mode6; sync; }
# UPDIR=/setting/updater   最后 ud_send_lsi.elf $mode_num 把模式号通知 SoC
```

`up.sh` 同时还能**直接改版本/机型/区域**（这就是"免校验版本之类的写入"的实现）：

```sh
func_modver()  { ud_datcnv.elf -e -i upnewver -o /setting/updater/dat4 -a }  # 写版本号
func_modmod()  { func_com_mod_modreg "model"  "dat2" $1 }                    # 写机型
func_modreg()  { func_com_mod_modreg "region" "dat3" $1 }                    # 写区域
```

### 相机内置的服务/工厂工具（`nflasha1` 的 initrd 里）

| 工具 | 用途 | 证据 |
| --- | --- | --- |
| **`ud_get_mem2file.elf`** | **读内存到文件** | `Usage: ud_get_mem2file.elf <filename> <address> <size>` |
| `ud_datcnv.elf` | dat 文件读/写（版本/机型/区域） | `up.sh` 里调用 |
| `crypter.elf` | **相机端 FDAT 解密器** | `DecryptorModule` / `MsDecryptorModule` / `CryptFileInputUpdaterManager`；**无 Encryptor 类** |
| `ud_cons.elf` | 串口控制台开关 | `is_valid_boot.sh` |
| `sdfileinput.elf` / `sauu` | 从 SD 卡升级 | `libsdfileinputupdaterapi.so` 含 `VersionSkip` |
| `hash_check.sh` | 升级必要性判定 | **`md5sum -c`** |
| `is_valid_boot.sh` | 引导来源判定 | 1=OneNAND / 2=MS(记忆棒) / 3=**USB Boot** |

### 关于 `ff01` / `ff03`

- 固件里**真实存在**的 `FF01` 只有一处：`libInfraBluetoothLowEnergy.so` 里的 **BLE 特征**
  （服务 UUID `8000FF00FF00FFFFFFFFFFFFFFFFFFFF`，特征 `FF01` 命令 / `FF02` 状态）
- 全固件搜 `ff03`：**零命中**；`ff01` 除 BLE 外无命中
- ⇒ 朋友所说的"ff01 读文件 / ff03 读内存"是**工厂服务工具侧的协议命令码**（配合 Service 模式 mode2 使用），
  而**对应能力**在固件里确实存在（`ud_get_mem2file.elf` 读内存、`sdfileinput.elf` 读文件）

### 关键结论

1. **`func_verskip` = 写 `/setting/updater/mode6` → 下次启动进 Version Skip 模式**（免版本校验）✅
2. 标志文件在 **`/setting` = VFAT 明文分区（nflasha2）** ⇒ **能写相机存储就能进该模式**
3. 升级包路线依旧不可逆（RSA-2048 签名，`crypter.elf` 只有解密器）——但**改相机不必走升级包**

### 工厂/服务路径全景（决定性证据）

```
PC 端工厂工具（★ 未随固件分发）
   │ USB
   ▼
相机（更新模式）：usb_extcmd.ko ("USB Extension Command function driver")
                 usbg_storage.ko (海量存储)   usbg_sen.ko (SEN)
   ▼
相机内：sauu（更新核心：-m Minor / -s VersionSkip）  mf（工厂工具）  sen.elf（Senser 工具）
```

**`mf`（39960 字节，ARM EABI5 ELF）里的关键符号**：

| 符号 | 含义 |
| --- | --- |
| `N7Updater21SignatureVerificationE` | **签名验证类** |
| `PEM_read_RSA_PUBKEY` / `RSA_verify` / `SHA1_*` / `SHA256` | **RSA 验签（SHA-1/SHA-256）** |
| `"Signature Verification Error"` | 验签失败提示 |
| `N7Updater12ModeFlagFileE` / `Mode3FlagFile` / `Mode5FlagFile` / **`Mode6FlagFile`** / `CompleteFlagFile` | 各类**模式标志文件**读写（含 Version Skip）|
| `/setting/updater/mode1`、`/ramdsk/mode5`、`/tmp_updater/fifo1|2` | 标志文件与 FIFO 协议路径 |

**`sen.elf`（22604 字节）**：路径 `/setting/.hasp`、`/boot/sen/smode`、`/setting/sen/smode`、`/var/.sen/.smode`，
符号 `Read_Hasp` / `Hasp Read Failed` / `LoadSenserMode` / `StoreSenserModeFile`
⇒ **Senser 工具需要 HASP 加密狗**。

**`ud_get_mem2file.elf`**：`Usage: ud_get_mem2file.elf <filename> <address> <size>`
⇒ "读内存"能力的实现；`execute_prod.sh` 里就用它 dump DDR training data。

### 由此得出的结论

1. **升级包不可逆**：`mf` 做 RSA 验签 + `crypter.elf` 只有解密器（无 Encryptor）
2. **"ff01/ff03 读文件/读内存" = 工厂服务工具协议**：相机端能力齐全
   （EXTCMD 通道 + 读内存工具 + SD/文件输入），但 **PC 端工具未分发**，且 `sen.elf` 需 **HASP 加密狗**
   —— 这正是"像 M5 服务模式一样不可逆"的含义
3. **自己复刻这条路**：需自己实现 PC 端客户端并逆向 `mf` 的 FIFO/EXTCMD 协议（ARM 32 位，可反汇编）
4. **最稳的路仍是硬件直写明文分区**

## 四、风险（务必看）

1. **刷改过的固件 = 变砖风险**。ZV-E10 的救砖路径（恢复模式/fastboot）**没有任何公开资料**
2. 相机端可能还有我们没看到的校验（镜像内部签名/一次性计数等）——
   建议先把 `updater.img_unpacked/bin/*.sh` 全读一遍，确认完整校验链
3. 先做**最小改动验证**（例如只改一个字符串/图标），成功后再谈实质修改
4. 现行固件若已是 2.03，官方升级程序会拒绝重刷同版本 → 需要改版本号或直接走相机内升级路径

## 五、资产清单（本次产出）

| 文件 | 说明 |
| --- | --- |
| `ufu_tool.py` | UFU 容器解析/校验/提取/重打包（CRC32 自动重算） |
| `unpacked_zve10/` | **解密解包后的完整 ZV-E10 Ver.2.03 固件**（1.3 GB） |
| `fwtool_a7iv/` | 带 CXD90045_k0/k8 与 CXD90057 密钥的 fwtool 工作副本 |
| 本报告 | 完整链路与风险说明 |
