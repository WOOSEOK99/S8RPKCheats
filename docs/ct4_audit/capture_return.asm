; Offline ABI reproduction: preserve the complete RAX returned by the real helper.
PUBLIC CaptureConnectedReturn
.code
CaptureConnectedReturn PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov rax, rcx
    mov rcx, rdx
    mov rdx, r8
    call rax
    add rsp, 28h
    ret
CaptureConnectedReturn ENDP
END
