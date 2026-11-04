#!/bin/bash
arm-linux-gnueabi-objdump -dr /mnt/g/WorkBuddy/zve10/work/grm_gles.ko --start-address=0x8e4 --stop-address=0x9c0 2>/dev/null | tail -n +8
