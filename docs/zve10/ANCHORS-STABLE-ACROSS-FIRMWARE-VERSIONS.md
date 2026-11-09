# Every anchor is identical across both known library versions

## The worry

The camera's own libraries were never hashed directly. Everything this project
anchors on came from firmware packages, and there are two distinct builds of
`libObj.so` on this machine:

```
12cf982dd3eaded4   21,305,528 B   from the firmware packages
                                  (unpacked_zve10, ZVE10_V203_unpacked,
                                   pve_backup/mirror_usr, COMPARE/extracted/ZVE10)
0b4be3fb599b539c   21,305,456 B   from a 2025 dump of an actual camera
                                  (ref-qn06142-zv-e10-re/dumps/camera_2025)
```

72 bytes apart. If the camera runs the second build and the anchors were taken
from the first, every hook would miss.

## Checked

| anchor | firmware build | camera dump |
|---|---|---|
| `libObj.so + 0x626538` | `90b583b000af04463846eaf7` | `90b583b000af04463846eaf7` |
| `libObj.so + 0x6266d0` | `f0b500f1980685b004460d46` | `f0b500f1980685b004460d46` |

| anchor | firmware `viewUnified2.so` | camera `viewUnified2.so` |
|---|---|---|
| `+ 0x3014f0` (populate) | `f0b58bb000af07f128010023…` | `f0b58bb000af07f128010023…` |
| `+ 0x2fccc0` (main) | `2de9f04100af044616460d46…` | `2de9f04100af044616460d46…` |
| `+ 0x862130` (row) | `2de9f04f8bb000af80468946…` | `2de9f04f8bb000af80468946…` |

And `viewUnified2.so` is the same size in both, 14,284,732 bytes.

All five anchors are byte-identical. The 72-byte difference between the two
`libObj.so` builds lies somewhere that does not move these offsets.

## Also checked: `CautionConfig.so`

The installer requires the `CmnViewSettingNode` constructor to resolve at
`base + 0xb8eb21` and the hook byte pattern at `base + 0xb8dffa` to match
`03 68 2d e9 f0 41 00 af d3 f8 e8 30`. QEMU confirms this holds at runtime:

```
NM caution sym=0x33246b21 base=0x326b8000 off=0xb8eb21 off_even=0xb8eb20
NM caution anchor OK
```

in both route A and route B, on both `native-ui.cpio` and the recovered
route-B rootfs.

## What is still unconfirmed

Only the camera's own hashes, which need the camera online. `verify_after_deploy.sh`
prints them first, and lists the expected values, so a mismatch is obvious rather
than mysterious:

```
libObj.so       12cf982dd3eaded426b0b9c703a681cd   (or 0b4be3fb, both work)
viewUnified2.so edb07179859e2fb6…
```

If the camera's `libObj.so` turns out to be a third build, the check to run is
simply whether `0x626538` still reads `90b583b0…` — and the log line to look for
is `NM caution anchor OK`, which the installer only prints after the byte
comparison passes.
