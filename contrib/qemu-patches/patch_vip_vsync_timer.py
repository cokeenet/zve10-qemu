#!/usr/bin/env python3
"""给 bionz_vip.c 打补丁：加 vsync 定时器 + 通道写入日志"""
import sys

path = '/root/qemu-bionz/hw/display/bionz_vip.c'
src = open(path).read()

# 1. 加 include
old = '#include "qemu/log.h"'
new = '#include "qemu/log.h"\n#include "qemu/timer.h"'
assert old in src, 'include not found'
src = src.replace(old, new, 1)

# 2. 加 vsync_timer 字段到 VipState
old = '    uint32_t reg_ctrl_intsts;\n    uint32_t reg_ctrl_en;\n    uint32_t reg_bg;\n} VipState;'
new = '    uint32_t reg_ctrl_intsts;\n    uint32_t reg_ctrl_en;\n    uint32_t reg_bg;\n    QEMUTimer *vsync_timer;\n} VipState;'
assert old in src, 'VipState not found'
src = src.replace(old, new, 1)

# 3. 加 vsync tick 回调（放在 vip_vsync 函数前面）
old = 'static void vip_vsync(void *opaque, int irq, int level)'
new = '''static void vip_vsync_tick(void *opaque)
{
    VipState *s = opaque;
    vip_vsync(s, 0, 1);   /* vsync 上升沿 -> 刷新显示 + 通道完成中断 */
    vip_vsync(s, 0, 0);   /* vsync 下降沿 */
    timer_mod(s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 16);  /* ~60Hz */
}

static void vip_vsync(void *opaque, int irq, int level)'''
assert old in src, 'vip_vsync not found'
src = src.replace(old, new, 1)

# 4. 在 vip_realize 里启动定时器
old = '    assert(s->memory && memory_region_is_ram(s->memory));\n    memory_region_set_log(s->memory, true, DIRTY_MEMORY_VGA);'
new = '''    assert(s->memory && memory_region_is_ram(s->memory));
    memory_region_set_log(s->memory, true, DIRTY_MEMORY_VGA);

    /* patch: 自产 vsync（CXD90045 没连 sysv）*/
    s->vsync_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, vip_vsync_tick, s);
    timer_mod(s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);'''
assert old in src, 'vip_realize tail not found'
src = src.replace(old, new, 1)

# 5. 给 vip_ch_write 加日志
old = '''static void vip_ch_write(VipState *s, unsigned int ch, hwaddr offset, uint64_t value, unsigned size)
{
    VipChannel *channel = &s->channels[ch];

    switch (offset) {
        case 0x00:
            channel->ctrl = value;
            break;

        case 0x20:
            channel->addr = value;
            break;

        case 0x24:
            channel->num_cpy = value;
            break;

        case 0x2c:
            channel->num_repeat = value;
            vip_update_display(s);
            break;'''
new = '''static void vip_ch_write(VipState *s, unsigned int ch, hwaddr offset, uint64_t value, unsigned size)
{
    VipChannel *channel = &s->channels[ch];

    fprintf(stderr, "VIPW ch=%u off=%#x val=%#x\\n", ch, (unsigned)offset, (unsigned)value);

    switch (offset) {
        case 0x00:
            channel->ctrl = value;
            break;

        case 0x20:
            channel->addr = value;
            break;

        case 0x24:
            channel->num_cpy = value;
            break;

        case 0x2c:
            channel->num_repeat = value;
            vip_update_display(s);
            break;'''
assert old in src, 'vip_ch_write not found'
src = src.replace(old, new, 1)

open(path, 'w').write(src)
print('补丁完成: vsync 定时器 + 通道日志')
