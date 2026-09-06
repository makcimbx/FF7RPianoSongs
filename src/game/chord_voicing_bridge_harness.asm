OPTION CASEMAP:NONE
EXTERN ff7rp_chord_copy_bridge:PROC
PUBLIC ff7rp_chord_bridge_probe
PUBLIC ff7rp_chord_probe_return
.code
SAVE_XMM MACRO reg, off
    movaps [rsp+off], reg
    .savexmm128 reg, off
ENDM
CHECK_REG MACRO reg
    cmp reg, 11223344h
    jne bad
ENDM
CHECK_XMM MACRO reg
    pcmpeqb reg, xmm0
    pmovmskb eax, reg
    cmp eax, 0ffffh
    jne bad
ENDM
ff7rp_chord_bridge_probe PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 0e8h
    .allocstack 0e8h
    SAVE_XMM xmm6, 40h
    SAVE_XMM xmm7, 50h
    SAVE_XMM xmm8, 60h
    SAVE_XMM xmm9, 70h
    SAVE_XMM xmm10, 80h
    SAVE_XMM xmm11, 90h
    SAVE_XMM xmm12, 0a0h
    SAVE_XMM xmm13, 0b0h
    SAVE_XMM xmm14, 0c0h
    SAVE_XMM xmm15, 0d0h
    .endprolog
    mov [rsp+30h], r8
    mov [r8+10h], rsp
    mov rbx, 11223344h
    mov rbp, rbx
    mov rsi, rbx
    mov rdi, rbx
    mov r12, rbx
    mov r13, rbx
    mov r14, rbx
    mov r15, rbx
    movq xmm6, rbx
    movaps xmm7, xmm6
    movaps xmm8, xmm6
    movaps xmm9, xmm6
    movaps xmm10, xmm6
    movaps xmm11, xmm6
    movaps xmm12, xmm6
    movaps xmm13, xmm6
    movaps xmm14, xmm6
    movaps xmm15, xmm6
    call ff7rp_chord_copy_bridge
ff7rp_chord_probe_return LABEL BYTE
    mov r10, [rsp+30h]
    mov [r10], rax
    mov [r10+8], rsi
    mov [r10+18h], rsp
    mov QWORD PTR [r10+20h], 0
    CHECK_REG rbx
    CHECK_REG rbp
    CHECK_REG rdi
    CHECK_REG r12
    CHECK_REG r13
    CHECK_REG r14
    CHECK_REG r15
    mov eax, 11223344h
    movq xmm0, rax
    CHECK_XMM xmm6
    CHECK_XMM xmm7
    CHECK_XMM xmm8
    CHECK_XMM xmm9
    CHECK_XMM xmm10
    CHECK_XMM xmm11
    CHECK_XMM xmm12
    CHECK_XMM xmm13
    CHECK_XMM xmm14
    CHECK_XMM xmm15
    mov QWORD PTR [r10+20h], 1
bad:
    movaps xmm6, [rsp+40h]
    movaps xmm7, [rsp+50h]
    movaps xmm8, [rsp+60h]
    movaps xmm9, [rsp+70h]
    movaps xmm10, [rsp+80h]
    movaps xmm11, [rsp+90h]
    movaps xmm12, [rsp+0a0h]
    movaps xmm13, [rsp+0b0h]
    movaps xmm14, [rsp+0c0h]
    movaps xmm15, [rsp+0d0h]
    add rsp, 0e8h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
ff7rp_chord_bridge_probe ENDP
END
