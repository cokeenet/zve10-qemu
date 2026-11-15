.syntax unified
.thumb
.section .text
.balign 4
.global mt_original_release
.hidden mt_original_release
.thumb_func
mt_original_release:
    push.w {r4,r5,r6,r7,r8,r9,r10,lr}
    sub sp, #0x20
    mov.w r8, #0x14
    movs r2, #1
    ldr.w pc, [pc, #0]
    .word 0x06309d55
.section .note.GNU-stack,"",%progbits
