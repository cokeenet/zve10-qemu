# Cave search on ZV-E10's viewUnified2.so: there is no usable cave

## What the A7M3 repository actually does

Reading `research/native_menu_loader.py` settles where their caves came from:

```python
RW_SHA='946cc404800c39e20a46099aa1f381cee4fe2b0e308946d5f572a5305b941f75'
original=ELF(a.usr/'lib/viewUnified2.so'); before=a.rw_view.read_bytes()
if sha(original.data)!=VIEW_SHA or sha(before)!=RW_SHA: raise ValueError(...)
for name,(at,capacity) in SITES.items():
  if name!='menu_hook' and before[at:at+capacity]!=b'\0\xbf'*(capacity//2):
      raise ValueError('Reserved RW cave mismatch')
```

`before` is `.lab/creative-icons-belt/viewUnified2.so` — their own previously modified
image, not the factory file. The check `b'\0\xbf'*n` is "the cave is already NOPs
going in", i.e. **they reserved the cave in an earlier edit of their own.** The factory
image has no such space to hand over.

## The measurement on our library

`ref_e10/usr/lib/viewUnified2.so`, 14,284,732 bytes:

```
RX LOAD  file/VA 0x000000 .. 0xB78AF0
RW LOAD  file 0xB78AF0, VA 0xB80AF0
.text    VA/off 0x18EEC8 .. 0x8D9E34   (7,647,084 bytes)
.rodata  VA/off 0x8D9E40 .. 0xAC2588
```

Scanned results, all negative:

| Search | Result |
|---|---|
| `.text` zero runs >= 20 bytes, any alignment | **0** |
| `.text` zero runs >= 16 bytes, 2-byte aligned | **0** |
| Thumb NOP runs (`00 BF`, >= 8 bytes) | **0** |
| `mov r8,r8` NOP runs (`C0 46`, >= 4) | **0** |
| zero runs >= 8 bytes, 4-byte aligned | 139, but the largest is only 14 bytes |
| zero runs >= 8 bytes, 2-byte aligned | 9, largest 14 bytes |

The longest candidates are **floating-point constant pools, not padding**:

```
VA 0x00846E30: 0000000000000000  0000000000005940  0000000000004940
                = 0.0             = 100.0            = 50.0
```

Writing code there would corrupt computed constants. Every other short zero run
sits inside instruction streams.

The `.text` tail padding is 8 bytes (`0x8D9E32 .. 0x8D9E3A`), nowhere near the
52 and 82 bytes the A7M3 loader needs. The program header table is also full
(`e_phoff 0x34`, 5 * 32 bytes ending exactly at `0xD4` where `.gnu_hash` begins),
so a new `LOAD` segment cannot be declared without moving sections.

## Why this does not block us

The A7M3 cave approach patches **the file on disk**, so it needs empty space in
that file. Our `ne_hook` patches **the function in memory**:

```
1. verify 12 expected bytes at the target
2. mmap a private RWX page, copy those 12 bytes there
3. append a jump back to (target + 12)
4. mprotect the target's page writable
5. write "ldr pc, [pc, #0]" + the address of the trampoline
6. mprotect the page back to R+X
```

That needs no spare space at all — only the right to `mprotect` the page the
function lives on. So the cave investigation has no bearing on our delivery
mechanism; it was worth checking, and the answer is "not applicable here".

The blocking problem remains the one already measured: `im.elf` is restored from
the warm-boot snapshot rather than exec'd, so its loader never reads
`/etc/ld.so.preload`. Hooking technique was never the obstacle.
