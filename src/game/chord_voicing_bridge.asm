OPTION CASEMAP:NONE
EXTERN ff7rp_chord_copy_dispatch:PROC
PUBLIC ff7rp_chord_copy_bridge
PUBLIC ff7rp_chord_copy_bridge_end
.code
; Normal Win64 boundary: shadow space plus the dispatch output span pointer.
; RSI is not touched until dispatch (original once + projection) returns;
; the inspected outer native callback owns restoration of its caller's RSI.
ff7rp_chord_copy_bridge PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov r8, [rsp+38h]
    lea r9, [rsp+20h]
    call ff7rp_chord_copy_dispatch
    mov rdx, [rsp+20h]
    test rdx, rdx
    jz stock
    mov rsi, rdx
stock:
    add rsp, 38h
    ret
ff7rp_chord_copy_bridge ENDP
ff7rp_chord_copy_bridge_end LABEL BYTE
END
