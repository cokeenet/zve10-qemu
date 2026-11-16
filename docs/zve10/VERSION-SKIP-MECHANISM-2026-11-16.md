# How the version check is bypassed on this body — and what we actually did

Direct answer to "how did we force a firmware update past the version check".
Two different things get conflated in that question, so here they are
separately, each with the evidence trail.

## 1. The route that was armed but never executed

The official updater has a documented **VERSKIP** mode. The mechanism is entirely
file-flag based, and `/setting` (nflasha2) is a plain VFAT partition — so anyone
who can write that partition can arm it.

`/usr/bin/common.src`:

```
MODE_USER=0  MODE_PROD=1  MODE_SERVICE=2  MODE_MINOR=3  MODE_MSCRYPT=5  MODE_VERSKIP=6
```

`nflasha1`'s initrd `usr/bin/UdtrMain.sh`:

```sh
getCurrentMode()
{
	#Forced Production?
	test -f /setting/updater/mode  || { echo "[W] Forced Prod Mode."; touch /ramdsk/mode1 ; return $MODE_PROD; }

	#Minor?
	test -f /setting/updater/mode3 && { ...; return $MODE_MINOR; }
	#Service?
	test -f /setting/updater/mode2 && { ...; return $MODE_SERVICE; }
	#Versionskip?
	test -f /setting/updater/mode6 && { touch /ramdsk/mode6 /ramdsk/UFP ; return $MODE_VERSKIP; }
	#MS Crypt?
	test -f /setting/updater/mode5 && { ...; return $MODE_MSCRYPT; }
	...
}
```

Two details matter and both were easy to get wrong:

- **`mode` must exist first.** Without it `getCurrentMode` returns `MODE_PROD`
  immediately and never even looks at `mode6`.
- `mode6` selects `MODE_VERSKIP`, and `execute_ufp.sh` routes that to the
  version-ignoring updater:

```sh
elif [ $MODE_VERSKIP -eq $cur_mode ] ; then
	copyCurrentFirmwareInfo
	echo "[DETAIL] : exec sauu -s"
	LD_PRELOAD="/usr/lib/libSysDef.so" sauu -s || { echo "[Error] : error sauu" ; exit 1 ; }
```

`sauu -s` reaches `Updater::GetVersionSkipUpdater()` — that string is present in
the shipped `sauu` binary (`UdrAPI,... "IN GetVersionSkipUpdater()"`), which is
what makes the class exist rather than being an inference.

The convenience path is `up.sh`, which does exactly the two `touch`es:

```sh
func_verskip() { rm -f $UPDIR/mode*; touch $UPDIR/mode $UPDIR/mode6; sync; }
```

### What we did with it (2026-11-12)

`git show 55f92dc` — snapshot `/setting` (57 entries, 7,583,232 B,
md5 `a75af9e4…`) then armed VERSKIP on the live camera. `git show 264e566` —
backed up the 17 files the flash would destroy, because `part_image.sum` rewrites
nflasha5/15/7/3 and the VERSKIP path falls into `us_mkfs`'s default branch, which
full-formats the remaining partitions including `/etc` and `/setting`.

**Then the plan changed.** Rather than let the updater run, the four official
partitions were written directly over fastboot
(`git show 07707be`, `docs/zve10/FULL-STOCK-FLASH-2026-11-12.md`):

| partition | start | bytes | result |
|---|---|---:|---|
| nflasha15 (/usr) | 0x2D800000 | 249,105,408 | byte-exact |
| nflasha7 (/rootfs) | 0x0A400000 | 5,132,288 | byte-exact |
| nflasha3 (/system) | 0x01C00000 | 30,157,824 | byte-exact |
| nflasha5 (WBI) | 0x05800000 | 36,039,424 | byte-exact |

So **the version check was never actually bypassed by VERSKIP on this camera.**
The firmware got onto it by raw partition write, which does not consult version
data at all — `flash_partition.py` has zero version or signature logic
(`grep -c version` = 0). That is why the flash worked regardless of the camera
already being on 2.03.

Current marker state, recorded after that flash: `/setting/updater/` still holds
`dat4` and `mode6`, but `mode` is deleted, so the camera boots the normal system
rather than p1.

## 2. The actual reasons a same-version reflash is refused

There are two independent gates, and only one of them is a "version check":

- `hash_check.sh` compares `BODYDATA.DAT` against a stored `.md5` and prints
  `update not needed.` — this is the same-version refusal. It is a plain md5sum,
  not a signature.
- The `UFU` container itself: `DATV/PROV/UDID/FDAT/DEND`, where the final check is
  a **CRC32** over the preceding bytes (documented in
  `docs/analysis/system/BODYDATA_UPDATE_FLOW.md`). CRC32, not RSA — recomputable
  after edits. The *payload* inside `FDAT` is AES-CBC with `KEY_CXD90045`, and
  ZV-E10's `crypterName` is exactly `CXD90045`, so that is decryptable and
  re-encryptable with keys already in hand.

That is why the version question is not really a lock: the same-version refusal
is an md5 file, the container integrity is a CRC32, and the payload cipher is one
we can key.

## 3. What is still true, and the trap

`docs/FIRMWARE_REPORT.md` §四.4 notes that on stock 2.03 the official updater
refuses to reflash the same version, which is what VERSKIP is for. But VERSKIP
also triggers a full format of every partition it can see — that is the trap
recorded in both manifests, and the reason the backup was taken first. If the
intent is only to change `/usr` contents, raw partition write is strictly safer
than arming VERSKIP: it writes exactly one partition and touches no flags.

## Honest limits

- VERSKIP was **armed but never exercised**: no run reached
  `"[DETAIL] : exec sauu -s"`, and there is no `User Update OK` anywhere in the
  tree.
- Consequently I have no measurement of what `sauu -s` does to the partitions on
  this body. The format claim comes from reading `us_mkfs.sh`'s dispatch, not
  from watching it happen.
