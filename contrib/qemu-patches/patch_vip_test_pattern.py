#!/usr/bin/env python3
"""VIP 测试模式：写彩色测试图案到帧缓冲 + 配置通道"""
path = '/root/qemu-bionz/hw/display/bionz_vip.c'
src = open(path).read()

# 在 vip_realize 的 vsync timer 后面加测试图案
old = """    /* patch: 自产 vsync（CXD90045 没连 sysv）*/
    s->vsync_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, vip_vsync_tick, s);
    timer_mod(s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);"""

new = """    /* patch: 自产 vsync（CXD90045 没连 sysv）*/
    s->vsync_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, vip_vsync_tick, s);
    timer_mod(s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);

    /* patch: 测试图案 —— 证明显示通路（Kimi 选项：framebuffer 覆盖）*/
    {
        const hwaddr test_addr = 0x3F000000;
        const int W = 320, H = 240;
        uint16_t *fb = memory_region_get_ram_ptr(s->memory);
        uint16_t *p = fb + test_addr / 2;
        int x, y;
        /* 8 条竖彩条: 白 黄 青 绿 品 红 蓝 黑 */
        static const uint16_t bars[8] = {
            0xFFFF, 0xFF0F, 0xF0FF, 0xF00F, 0x0FFF, 0x0F0F, 0x00FF, 0x000F
        };
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                int bar = x * 8 / W;
                p[y * W + x] = bars[bar];
            }
        }
        /* 中央画一个白色矩形框 */
        for (y = 100; y < 140; y++) {
            for (x = 120; x < 200; x++) {
                if (y == 100 || y == 139 || x == 120 || x == 199)
                    p[y * W + x] = 0xFFFF;
            }
        }
        /* 配置 channel 2 (layer 1, RGBA4444) */
        s->channels[2].ctrl = 1;
        s->channels[2].addr = test_addr;
        s->channels[2].num_cpy = W * 2;
        s->channels[2].num_repeat = H - 1;
        fprintf(stderr, "VIPTEST: test pattern at %#lx, ch2 configured\\n",
                (unsigned long)test_addr);
    }"""

assert old in src, 'vsync timer block not found'
src = src.replace(old, new, 1)
open(path, 'w').write(src)
print('VIP test pattern patch applied')
