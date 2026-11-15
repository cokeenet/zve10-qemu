.syntax unified
.arm
.text
.global _start
.type _start,%function
_start:
 bl main
 mov r7, #1
 svc #0
.size _start,.-_start
.section .note.GNU-stack,"",%progbits
