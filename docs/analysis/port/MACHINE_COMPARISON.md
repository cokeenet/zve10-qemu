# Sony 机型对比（A6400 / ZV-E10 / ZV-1F / ZV-E10 II）

> 自动生成：`python3 <repo>/compare_machines2.py`；原始字段见 `MACHINE_COMPARISON.tsv`

## 1 身份与平台

### 身份

| 项 | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| SoC (chassis) | CXD90045 | CXD90045 | CXD90045 | CXD90057+CXD90058 |
| FDAT crypter | CXD90045_k0 | CXD90045_k0 | CXD90045_k8 | CXD90057_k8 |
| model id | 0x81030011 | 0x1030010 | 0x11020010 | 0x30030002 |
| 固件版本 | 2.00 | 2.03 | 2.01 | 1.02 |
| body_version | 100.101.010 | 100.110.035 | 100.101.033 | 700.113.051 |
| version.txt | 38111_03.2019052701 | 30110_06.2026060501 | 21110_06.2025042601 | 33002_03.2025101101 |


## 2 内核与 initrd（取自主 /system，不含升级器那份）

### 内核

| 项 | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| 内核版本字符串 | 3.0.27_nl-rt106+ (<BUILDER>@<BUILDHOST>) (gcc version 4.5.1 (20170415 (Sony CE Linux 8.2.5.7)) ) #1 SMP PREEMPT RT Mon May 27 14:09:40 JST 2019 | 3.0.27_nl-rt106+ (<BUILDER>@<BUILDHOST>) (gcc version 4.5.1 (20220614 (Sony CE Linux 8.2.7.6)) ) #1 SMP PREEMPT RT Fri Jun 5 03:41:12 JST 2026 | 3.0.27_nl-rt106+ (<BUILDER>@<BUILDHOST>) (gcc version 4.5.1 (20220614 (Sony CE Linux 8.2.7.6)) ) #1 SMP PREEMPT RT Sat Apr 26 03:02:54 JST 2025 | 4.9.337-scel+ (<BUILDER>@<BUILDHOST>) (gcc version 6.4.0 (20201211 (Sony CE Linux 14.0.1.0)) ) #1 SMP PREEMPT Sat Oct 11 04:27:40 JST 2025 |
| 文件类型 | data | data | data | Linux kernel ARM64 boot executable Image, little-endian, 4K pages |
| 字节 | 4085888 | 4117760 | 3804032 | 6610952 |
| kernel md5[:12] | c46b7442b507 | 7e42c73f86eb | 5b0f46742d3e | 168dd4e06baa |
| initrd 字节 | 3963904 | 2183168 | 2183168 | 5218304 |
| initrd md5[:12] | 98ccb49b1d00 | 12362d21e94b | adc8e1de08dd | 022b145cc7e7 |
| initrd 内 /sbin/init md5[:12] | f613d678791e | 0ff6dd725d90 | 486af5e902d7 | f30bdb79e59b |


升级器专用内核对（只有 ZV-E10 的包里带）：
| 项 | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| 含 0400_updater 分区 | False | True | False | False |
| updater vmlinux.bin 字节 | — | 1701156 | — | — |
| updater initrd.img 字节 | — | 4943872 | — | — |

## 3 分区与升级包内容

### 分区

| 项 | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| partinf 条目 | 30 | 30 | 30 | 30 |
| 有效分区数 | 16 | 16 | 16 | 17 |
| /usr MiB | 300.0 | 300.0 | 500.0 | 808.0 |
| /rootfs MiB | 8.0 | 8.0 | 8.0 | 16.0 |
| /wbi MiB | 60.0 | 60.0 | 60.0 | 300.0 |
| 有 android 分区 | yes | yes | yes | no |
| 包内分区镜像 | ['nflasha15', 'nflasha3', 'nflasha5', 'nflasha7'] | ['nflasha1', 'nflasha15', 'nflasha3', 'nflasha5', 'nflasha7'] | ['nflasha15', 'nflasha3', 'nflasha5', 'nflasha7'] | ['nflasha15', 'nflasha3', 'nflasha5', 'nflasha7', 'nflasha8'] |
| 有 0750_nor_loader | False | False | False | True |
| 有 0640_darwin | True | True | False | True |
| 有 1050_i2c | False | False | False | True |


## 4 用户态

### 用户态

| 项 | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| /usr 文件数 | 799 | 950 | 3946 | 1070 |
| /usr/lib 条目 | 126 | 149 | 141 | 203 |
| libc | libc-2.11.2.so | libc-2.11.2.so | libc-2.11.2.so | libc-2.25.so |
| 有 appFw.so | no | no | yes | yes |
| appFw 字节 | — | — | 53189440 | 93620512 |
| 有 libObj.so | yes | yes | yes | yes |
| viewUnified 数 | 7 | 7 | 0 | 0 |
| kmod 数 | 101 | 103 | 103 | 113 |
| share/app/*.uxc | 228 | 299 | 152 | 220 |
| sabin 文件 | 86 | 77 | 85 | 41 |
| codec 文件 | 6 | 6 | 6 | 3 |


## 5 模块表（libIMDB）几何

### libIMDB

| 项 | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| .data.rel.ro 字节 | 8844 | 9680 | 10164 | 17464 |
| imdb_raw 符号字节 | 132 | 132 | 132 | 17464 |
| 模块记录数 | 198 | 217 | 228 | 其他布局(0B) |


## 6 集合重合度（/usr/lib 文件名）

| A \ B | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| **A6400** | — | 111/164 (68%) | 89/178 (50%) | 75/254 (30%) |
| **ZV-E10** | 111/164 (68%) | — | 124/166 (75%) | 101/251 (40%) |
| **ZV-1F** | 89/178 (50%) | 124/166 (75%) | — | 116/228 (51%) |
| **ZV-E10 II** | 75/254 (30%) | 101/251 (40%) | 116/228 (51%) | — |

## 7 集合重合度（/usr/kmod 模块名）

| A \ B | A6400 | ZV-E10 | ZV-1F | ZV-E10 II |
| --- | --- | --- | --- | --- |
| **A6400** | — | 100/104 (96%) | 99/105 (94%) | 78/136 (57%) |
| **ZV-E10** | 100/104 (96%) | — | 102/104 (98%) | 79/137 (58%) |
| **ZV-1F** | 99/105 (94%) | 102/104 (98%) | — | 79/137 (58%) |
| **ZV-E10 II** | 78/136 (57%) | 79/137 (58%) | 79/137 (58%) | — |
