#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""backup.ko 探针补丁：backup_init(0x0) 空桩 -> b backup_osal_init(0x50c0)。
原因：backup_osal_init 负责注册 qid 0x3e014d/0x3e0150 的消息回调（func_cb_sync/async），
但它唯一的调用点在 backup_suspend(0xf4) 里 —— QEMU 无 PM 事件 ⇒ 永不注册 ⇒
用户侧 libbackup 的 sync 消息永远无人应答（ctor 崩溃根因）。
backup_init 是 udif 驱动结构体里的 init 回调，boot 时必被调用。
指纹：0x0 处 e1 a0 c0 0d（mov ip,sp）。目标：b 0x50c0 = EA00142E（小端 2e 14 00 ea）。
"""
import sys

def main() -> int:
    p = sys.argv[1]
    d = bytearray(open(p, 'rb').read())
    ORIG = bytes.fromhex('0dc0a0e1')
    NEW = bytes.fromhex('2e1400ea')   # b 0x50c0
    FOFF = 0x34   # .text 在文件偏移 0x34（vaddr 0）
    got = bytes(d[FOFF:FOFF+4])
    if got == NEW:
        print('already patched')
        return 0
    assert got == ORIG, '指纹不符 @file 0x34: %s' % got.hex()
    d[FOFF:FOFF+4] = NEW
    open(p, 'wb').write(d)
    print('backup_init -> b backup_osal_init 已打上')
    return 0

if __name__ == '__main__':
    sys.exit(main())
