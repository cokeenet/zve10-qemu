# Display Pipeline & Boot Factor Gate (2026-10-27)

From the ZV-1F new-menu port team. These are hardware-level findings relevant to anyone debugging the UI on this platform.

## Display Pipeline (QEMU CXD90045)

```
App (appFw/uxengine) → GPE/Sugilite GPU → cpyfb (blit engine) → VIP (scan-out) → LCD
       ↑                    ↑                    ↑                  ↑
  boot_factor gate     0xF1000000         0x79700000         0x79800000
  (see §3)            (ack-only stub)   (full impl)       (320x240 RGBA4444)
```

### QEMU model status

| Component | Address | Status | Notes |
|---|---|---|---|
| GPE/Sugilite | 0xF1000000 (IRQ 76) | Stub (ack-only) | Accepts command DMA, raises IRQ, but does NOT render. 19 test commands with magic `0x12345678`. |
| cpyfb | 0x79700000 / 0x79780000 | Full implementation | fill_rect / bit_blit / alpha_blend. 0 calls observed (App never triggers it). |
| VIP | 0x79800000 / 0x79800800 | Working (with vsync fix) | 320x240 QemuConsole, RGBA4444 / YCbCr422. 2 layers from channels 0 and 2. |
| vsync | — | **FIXED** | CXD90045 init is missing the `sysv→VIP` vsync connection. CXD4108 has it. Add a 60Hz QEMUTimer. |

### ★ VIP vsync fix (bionz_vip.c)

In `vip_realize()`, add after `memory_region_set_log`:

```c
s->vsync_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, vip_vsync_tick, s);
timer_mod(s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
```

And the callback (must re-configure channels each tick — enable is self-clearing):

```c
static void vip_vsync_tick(void *opaque)
{
    VipState *s = opaque;
    vip_vsync(s, 0, 1);  // rising edge: vip_update_display + clear enables
    vip_vsync(s, 0, 0);  // falling edge
    timer_mod(s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 16);  // ~60Hz
}
```

**Gotcha**: `vip_reset()` runs AFTER `vip_realize()` and zeroes all channel config. If you configure channels in realize, they get wiped. Configure in reset (after the zeroing) or re-configure every vsync tick.

### ★ Framebuffer test (proof of display pipeline)

Write RGBA4444 test pattern to physical `0x3F000000`, configure VIP channel 2:

```c
s->channels[2].ctrl = 1;
s->channels[2].addr = 0x3F000000;
s->channels[2].num_cpy = 320 * 2;   // WIDTH * 2 for RGBA4444
s->channels[2].num_repeat = 240 - 1; // HEIGHT - 1
```

Pixel format: `uint16_t`, R[15:12] G[11:8] B[7:4] A[3:0].

Verified: `screendump` shows colored bars. Display pipeline is fully functional.

## Boot Factor Gate (why the App shows nothing)

Even with 107 threads running and all services initialized, the App shows no UI because it is stuck at the **boot factor / Application_Boot_Select** stage.

Evidence from `appFw.so` strings:

```
"[Boot_Factor] bis:%08x attr:%08x ext_mode:%08x"
"[Boot_Factor] key:%08x type:%08x eecap:%08x"
"Not support boot key : %08x"
"Application_Boot_Select"
"sequence_boot boot_factor Attr=%d ee_cap = %d"
"Success get_boot_factor" / "Failure get_boot_factor"
```

C++ types: `model::boot_factor_t`, `model::upm_boot_key_t`, `model::sequence_boot`

Causal chain:

```
No input devices (/proc/bus/input/devices = empty)
  → UPM kernel module (118KB, loaded) gets no GPIO/ADC key events
    → get_boot_factor returns invalid / Failure
      → Application_Boot_Select doesn't know what mode to enter
        → State machine stays in boot phase, instantiates no screens
          → OnDemandLoader receives no load requests
            → uxengine never draws → black screen (but all threads alive)
```

**This is NOT a missing service.** All 107 threads are in normal idle state (futex_wait / osal_rcv_msg_tmo). The App needs a boot key (power button press) to decide what to display.

## Input Devices

`/proc/bus/input/devices` = empty. Handlers exist (sysrq, mousedev, evdev) but zero devices registered.
No I2C bus (`/sys/bus/i2c/devices/` does not exist).
To enable input injection, a GPIO-key device model needs to be added to bionz.c.

## Tools

- `patch_vip_vsync_timer.py` — Add vsync timer to VIP model
- `patch_vip_test_pattern.py` — Add test pattern rendering (proof of display)
- `patch_cpyfb_logging.py` — Add cpyfb command logging
- `run_screendump.sh` — Boot QEMU and capture VIP display output
- `fb2png.py` — Decode framebuffer RGBA4444/RGB565 to PNG
- `diag_app_observer.sh` — Comprehensive /proc observer (threads, wchan, input devices, modules)
