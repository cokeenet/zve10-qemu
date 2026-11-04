.syntax unified
.thumb

.section .text
.align 2
.type trace_init, %function
.thumb_func
trace_init:
    push {r4, r7, lr}
    adr r0, path
    movw r1, #1089
    movs r2, #0
    movs r7, #5
    svc #0
    mov r4, r0
    cmp r4, #0
    blt .Linit_return
    mov r0, r4
    adr r1, init_message
    movs r2, #(init_message_end - init_message)
    movs r7, #4
    svc #0
    mov r0, r4
    movs r7, #6
    svc #0
.Linit_return:
    pop {r4, r7, pc}
.size trace_init, . - trace_init

.section .init_array,"aw",%init_array
.align 2
    .word trace_init

.section .text
.global ioctl
.type ioctl, %function
.thumb_func
ioctl:
    push {r4, r5, r6, r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    movw r0, #10
    movt r0, #32768
    cmp r5, r0
    bne .Ldo_ioctl
    bl trace_init
    mov r2, r6
    mov r0, r4
    mov r1, r5
.Ldo_ioctl:
    movs r7, #54
    svc #0
    mov r6, r0
    cmp r6, #0
    blt .Lerror
    mov r0, r6
    b .Lreturn

.Lerror:
    movw r0, #10
    movt r0, #32768
    cmp r5, r0
    bne .Lset_errno

    adr r0, path
    movw r1, #1089
    movs r2, #0
    movs r7, #5
    svc #0
    mov r4, r0
    cmp r4, #0
    blt .Lset_errno

    adr r1, hex_digits
    adds r1, #7
    mov r3, r6
    movs r2, #8
.Lhex_loop:
    ands r0, r3, #15
    adds r0, #48
    cmp r0, #57
    ble .Lhex_digit
    adds r0, #39
.Lhex_digit:
    strb r0, [r1]
    subs r1, #1
    lsrs r3, r3, #4
    subs r2, #1
    bne .Lhex_loop

    mov r0, r4
    adr r1, message
    movs r2, #(message_end - message)
    movs r7, #4
    svc #0
    mov r0, r4
    movs r7, #6
    svc #0

.Lset_errno:
    bl __errno_location
    rsbs r1, r6, #0
    str r1, [r0]
    mvn r0, #0

.Lreturn:
    pop {r4, r5, r6, r7, pc}
.size ioctl, . - ioctl

.section .rodata
path:
    .asciz "/log/ioctl.log"

.section .data
message:
    .ascii "IOCTLERR ret=0x"
hex_digits:
    .space 8
message_end:
    .byte 10
init_message:
    .ascii "PRELOADOK\n"
init_message_end:

.section .note.GNU-stack,"",%progbits
