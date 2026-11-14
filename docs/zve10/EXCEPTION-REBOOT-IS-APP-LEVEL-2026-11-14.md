# `exception.reboot=1` fires on app crashes too, not just kernel oops

Answering whether a failed experiment would reboot the camera because the kernel
crashed or because the UI app crashed. It is the app.

## The chain, from source

`arch/arm/kernel/traps.c` splits at `arm_notify_die()`:

```c
if (user_mode(regs)) {
        ... force_sig_info(info->si_signo, info, current);   /* no die() */
} else {
        die(str, regs, err);
}
```

So a user-mode fault never reaches `die()` or `em_show()`. It becomes a normal
`SIGSEGV` delivery. But the process then dies, and `do_coredump()` runs.

`fs/exec.c` has `core_pattern[CORENAME_MAX_SIZE] = ""` under `CONFIG_EJ`, and
`do_coredump()` starts with:

```c
#ifdef CONFIG_EJ
        if ('\0' == core_pattern[0])
                goto fail;
#endif
```

Every `fail` path lands on:

```c
#ifdef CONFIG_EXCEPTION_MONITOR
        if (em_show_coredump(regs) == -1) ...
#endif
```

and `em_show_coredump()` calls `exception_check(mode, regs)`, which is
`em_exception_monitor()` once the exception module has initialised. That function
ends with:

```c
if (em_reboot_flag) {
        boot_time_add("Reboot by the flag");
        em_reboot(0);
}
```

`em_reboot_flag` is `module_param_named(reboot, ...)` in `kernel/exception.c` —
i.e. the `exception.reboot=` kernel argument.

## Values on the real camera

```
/proc/sys/kernel/core_pattern                 = (empty)
/sys/module/exception/parameters/reboot       = 1
/sys/module/exception/parameters/segv_exit    = 0
/sys/module/exception/parameters/oom_exit     = 0
exception_check_mode                          = 3   (default, no em= override)
```

Empty `core_pattern` sends every app crash straight to `goto fail`, so
`em_show_coredump()` runs on every crash, and `reboot=1` turns it into a full
machine restart.

## What this means for a memory-injection experiment

If the injected stub is wrong and `im.elf` faults, the camera restarts. That is
recoverable — it is the same restart we have already survived several times —
because the flash contents are untouched. The restart is not caused by bad data
on disk; it is caused by the app dying with `exception.reboot=1`.

## Capability check for the safer path

Rather than patching `im.elf` text (which is what faults if the stub is wrong),
the process can be driven through `ptrace`. Attach and detach were verified
against the live UI process with no modification:

```
PTRACE_ATTACH: rc=0  (stopped, sig=19)
PC=0x0438B17C SP=0x5EC91C04 LR=0x040B4721 R0=0xFFFFFC03
PTRACE_DETACH: OK (process resumed, unmodified)
pid 157 still alive afterwards
```

`/proc/157/mem` also opens `O_RDWR` and accepts writes even to `r-xp` pages
(`FOLL_FORCE`), which is the same mechanism gdb uses for breakpoints. So both a
`ptrace` route and a `/proc/pid/mem` route are available; neither needs preload,
`exec`, or a cold boot.
