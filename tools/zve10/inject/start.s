    .syntax unified
    .text
    .global _start
    .type _start, %function
_start:
    mov  r0, sp
    ldr  r0, [r0]          @ argc
    mov  r1, sp
    add  r1, r1, #4        @ argv
    bl   main
    mov  r7, #1            @ SYS_exit
    svc  #0
    b    .
