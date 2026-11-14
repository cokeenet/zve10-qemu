# Backup properties are readable from the agent; all three were already set

## The toolkit is not required for this

`libbackup.so` fails to `dlopen` on its own:

```
dlopen failed: /usr/lib/libbackup.so: undefined symbol: osal_reg_msg_queue_cb
```

The symbol is exported by `libosal_uipc.so`, so preloading the three osal
libraries with `RTLD_GLOBAL` in dependency order is enough:

```
dlopen("/usr/lib/libosal_utm.so",    RTLD_NOW|RTLD_GLOBAL)  OK
dlopen("/usr/lib/libosal_uipc.so",   RTLD_NOW|RTLD_GLOBAL)  OK
dlopen("/usr/lib/libosal_ulogio.so", RTLD_NOW|RTLD_GLOBAL)  OK
dlopen("/usr/lib/libbackup.so",      RTLD_NOW|RTLD_GLOBAL)  OK
```

Then `Backup_read(id, buf, len)` returns 1 on success. This means the whole
PMCA/Jiritsu service session is unnecessary for both reading and writing camera
Backup properties: a small ARM binary run by the root agent is enough. That
matters because the Jiritsu backend dies at loader upload with
`GenericUsbException`, and the old terminal backend returns `0x80` with no data
on 2.03.

## All three properties were already correct

```
id=0x01071C54  rc=1  data=01
id=0x00A5000B  rc=1  data=02
id=0x00A5000C  rc=1  data=16
```

These are the values the 5 GHz procedure wants. They were written on 2026-11-08
and every later operation left `/setting` alone — the full `/usr` flashes do not
touch `nflasha2` — so they survived.

## The patched library is live in memory too

```
mapping: 16dd8000-17d68000 r-xp 00000000 fc:0f 46832  /usr/lib/CautionConfig.so
live byte @0x17B4F8E4 = 0x1B  (PATCHED TypeNB)
```

Read through `/proc/157/mem` at `mapping_base + 0xD778E4`. The inode on disk is
46832 as well, so the running process maps our patched file, not a stale copy.

## Consequence

Both layers the procedure depends on are in place and active:

1. `CautionConfig.so` page selector = `0x1B` (TypeNB, has the band row), live in
   the UI process.
2. Backup properties `01 / 02 / 16`, readable through the native API.

So no further write is needed to reach the goal. The remaining check is
behavioural: whether `MENU -> 网络 -> Wi-Fi 设置` now shows the band row.
