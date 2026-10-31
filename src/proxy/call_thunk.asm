; Restores the original CALL stack, then tail-jumps to its authentic helper.
option casemap:none
EXTERN ff7rp_proxy_call_dispatch:PROC
EXTERN ff7rp_proxy_call_resume:QWORD
PUBLIC ff7rp_proxy_call_thunk
PUBLIC ff7rp_proxy_call_dispatch_return
FRAME_BYTES EQU 2a0h
.code
ff7rp_proxy_call_thunk PROC FRAME
    push rbp
    .pushreg rbp
    lea rsp,[rsp-FRAME_BYTES]
    .allocstack FRAME_BYTES
    lea rbp,[rsp]
    .setframe rbp,0
    mov [rbp+20h],rax
    mov [rbp+28h],rcx
    mov [rbp+30h],rdx
    mov [rbp+38h],rbx
    .savereg rbx,38h
    mov [rbp+40h],rsi
    .savereg rsi,40h
    mov [rbp+48h],rdi
    .savereg rdi,48h
    mov [rbp+50h],r8
    mov [rbp+58h],r9
    mov [rbp+60h],r10
    mov [rbp+68h],r11
    mov [rbp+70h],r12
    .savereg r12,70h
    mov [rbp+78h],r13
    .savereg r13,78h
    mov [rbp+80h],r14
    .savereg r14,80h
    mov [rbp+88h],r15
    .savereg r15,88h
    fxsave64 [rbp+0a0h]
    .savexmm128 xmm6,1a0h
    .savexmm128 xmm7,1b0h
    .savexmm128 xmm8,1c0h
    .savexmm128 xmm9,1d0h
    .savexmm128 xmm10,1e0h
    .savexmm128 xmm11,1f0h
    .savexmm128 xmm12,200h
    .savexmm128 xmm13,210h
    .savexmm128 xmm14,220h
    .savexmm128 xmm15,230h
    .endprolog
    pushfq
    pop qword ptr [rbp+90h]
    cld
    call ff7rp_proxy_call_dispatch
ff7rp_proxy_call_dispatch_return LABEL BYTE
    test rax,rax
    jz fatal_call
    cmp rax,qword ptr [ff7rp_proxy_call_resume]
    jne fatal_call
    fxrstor64 [rbp+0a0h]
    mov rax,[rbp+20h]
    mov rcx,[rbp+28h]
    mov rdx,[rbp+30h]
    mov rbx,[rbp+38h]
    mov rsi,[rbp+40h]
    mov rdi,[rbp+48h]
    mov r8,[rbp+50h]
    mov r9,[rbp+58h]
    mov r10,[rbp+60h]
    mov r11,[rbp+68h]
    mov r12,[rbp+70h]
    mov r13,[rbp+78h]
    mov r14,[rbp+80h]
    mov r15,[rbp+88h]
    push qword ptr [rbp+90h]
    popfq
    lea rsp,[rbp+FRAME_BYTES]
    pop rbp
    jmp qword ptr [ff7rp_proxy_call_resume]
fatal_call:
    mov ecx,7
    int 29h
ff7rp_proxy_call_thunk ENDP
END
