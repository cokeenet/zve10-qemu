.syntax unified
.thumb
.text
.balign 4
.global submit_fixture_start, submit_fixture_end
submit_fixture_start:
 push.w {r4,r5,r6,r7,r8,lr}
 sub sp,sp,#16
 mov r5,r0
 add r7,sp,#16
 ldr r3,[r0,#48]
 movs r0,#77
 mov sp,r7
 pop.w {r4,r5,r6,r7,r8,pc}
submit_fixture_end:
.balign 4
.global wait_fixture_start, wait_fixture_end
wait_fixture_start:
 push {r4,r5,r6,r7,lr}
 sub sp,sp,#12
 subs r4,r0,#1
 add r7,sp,#0
 cmp r4,#7
 bhi.n wait_unknown
 mov.w r0,#0x40000000
 b.n wait_return
 .rept 7
 nop
 .endr
wait_unknown:
 movs r0,#0
wait_return:
 add r7,r7,#12
 mov sp,r7
 pop {r4,r5,r6,r7,pc}
wait_fixture_end:
.section .note.GNU-stack,"",%progbits
