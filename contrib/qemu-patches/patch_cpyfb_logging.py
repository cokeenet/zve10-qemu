#!/usr/bin/env python3
"""给 bionz_cpyfb.c 加日志"""
path = '/root/qemu-bionz/hw/dma/bionz_cpyfb.c'
src = open(path).read()

old = 'static void cpyfb_ch_write(CpyfbState *s, unsigned int ch, hwaddr offset, uint64_t value, unsigned size)\n{\n    CpyfbChannel *channel = &s->channels[ch];\n\n    switch (offset) {'
new = 'static void cpyfb_ch_write(CpyfbState *s, unsigned int ch, hwaddr offset, uint64_t value, unsigned size)\n{\n    CpyfbChannel *channel = &s->channels[ch];\n\n    fprintf(stderr, "CPYFBW ch=%u off=%#x val=%#x\\n", ch, (unsigned)offset, (unsigned)value);\n\n    switch (offset) {'
assert old in src, 'cpyfb_ch_write not found'
src = src.replace(old, new, 1)

old = 'static void cpyfb_command(CpyfbState *s)\n{'
new = 'static void cpyfb_command(CpyfbState *s)\n{\n    fprintf(stderr, "CPYFB cmd ch0=%#x/%#x/%#x ch1=%#x/%#x/%#x ch2=%#x/%#x/%#x\\n",\n        s->channels[0].ctrl, s->channels[0].addr, s->channels[0].num_cpy,\n        s->channels[1].ctrl, s->channels[1].addr, s->channels[1].num_cpy,\n        s->channels[2].ctrl, s->channels[2].addr, s->channels[2].num_cpy);'
assert old in src, 'cpyfb_command not found'
src = src.replace(old, new, 1)

open(path, 'w').write(src)
print('cpyfb logging added')
