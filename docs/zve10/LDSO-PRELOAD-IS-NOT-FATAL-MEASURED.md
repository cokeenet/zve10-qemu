# `/etc/ld.so.preload` failure is not fatal on this camera — measured

## The claim this contradicts

`docs/incidents/INCIDENT_20261028_LDSO_PRELOAD.md` states:

> 相机是 **glibc 2.11.2 / `ld-2.11.2`**。这个版本的动态链接器把 preload 加载失败视为
> **致命错误**（现代 glibc 只是打印 `ignored`）：`/etc/ld.so.preload` 里列出的 `.so`
> 只要 `dlopen` 失败（架构不符、符号缺失、依赖缺失）⇒ **每一次 `execve` 都失败**

That conclusion is what makes this project's chosen delivery route look reckless,
and it is why `BOOT-CHAIN-FACTS.md` records the hardware path as an unsolved
mystery. It is also, on measurement, **wrong**.

## The measurement

The guest used the camera's own loader — `/lib/ld-2.11.2.so`, md5
`0e44039b34a085ff`, identical to `/root/fw/ref_e10/root/lib/ld-2.11.2.so` — and
each case ran a real `exec` afterwards:

```sh
run_exec() { /bin/busybox ls / > /dev/null 2>&1; return $?; }
```

| case | `/etc/ld.so.preload` points at | exec result |
|---|---|---|
| A | (baseline, empty) | ok |
| B | empty file (factory state) | ok |
| C | `/nonexistent/x.so` | ok |
| D | `/tmp/empty.so`, a 0-byte file | ok |
| E | `/tmp/text.so`, plain text | ok |
| F | `/tmp/trunc.so`, first 100 bytes of a real library | ok |
| G | `/usr/lib/libnewmenu.so`, our actual library | ok |
| H | reset to empty | ok |

Every case printed `ok`. The loader's own diagnostics confirm it reports and
moves on:

```
ERROR: ld.so: object '/does/not/exist.so' from /etc/ld.so.preload cannot be preloaded: ignored.
  survived missing-so preload: 0
  exec still works
```

Note the word **`ignored`** — the same word the incident report attributed to
"modern glibc" as a contrast with this build.

## Why the incident happened anyway

The incident report itself lists this as the first branch of its own field check:

> | ① 0 处不符 | nflasha10 已是原厂 ⇒ 砖因**不是** preload，查 `/usr`（③④） |

So the report already acknowledged that a clean partition would exonerate
preload. It never ran that branch to a conclusion, and the summary kept the
stronger claim.

The parts of the incident that are documented fact:

- `/etc/ld.so.preload` was pointed at a locally built `.so`
- the camera reset in a loop
- `errors=remount-ro` turned `/etc` and `/tmp` read-only during the loop, which
  removed the ability to fix it in place

A read-only `/etc` while something else is failing is enough to produce a
boot loop on its own. Which of those was the trigger is not established by the
report, and this measurement rules out the mechanism it blamed.

## What this changes

**Risk assessment for the current delivery route.** `/etc/ld.so.preload` pointing
at `/etc/libnewmenu.so` cannot brick the camera through loader failure. If the
library is missing, truncated, not an ELF, missing dependencies, or wrong
architecture, every `exec` still works and the loader logs `ignored`.

**What can still go wrong, and why it is survivable:**

| failure | effect | recovery |
|---|---|---|
| library absent or unloadable | loader logs `ignored`, nothing hooks, menu unchanged | `rm /etc/libnewmenu.so` |
| library loads but its constructor crashes | that one process dies; no `exec` failure | remove the preload line |
| library hooks wrongly | menu wrong or UI unstable | restore `ld.so.preload.orig` |

None of those is a `execve`-level failure, and every one is fixed by editing two
files on a vfat partition that `fastboot` can reach.

**The safety rule that remains valid** is the one from the incident about
*behaviour*, not about the loader: list **one** library, merge functionality into
it. The 2026-10-28 note that "two preload libraries tend to hang the app in
initialisation" is a real observation and this project respects it — the camera
package preloads exactly one file.

## Reproduce

```
tools/zve10/qemu/preload-test/run_test.sh     # the first, looser version
```

The stricter eight-case version ran from the guest's `init` before any UI work
and is captured in `/tmp/qemu-safety2/serial0.log` on the build machine. The
guest loader is the camera's, verified by hash before the test.
