; Assembled at cave+0x2400; helper at cave+0x874.
pushfq
push rax
push rcx
push rdx
push r8
push r9
push r10
push r11
sub rsp,0x88
movdqu [rsp+0x20],xmm0
movdqu [rsp+0x30],xmm1
movdqu [rsp+0x40],xmm2
movdqu [rsp+0x50],xmm3
movdqu [rsp+0x60],xmm4
movdqu [rsp+0x70],xmm5
lock inc dword ptr [rip+seen]
call 0x874
test eax,eax
jz blocked
movdqu xmm0,[rsp+0x20]
movdqu xmm1,[rsp+0x30]
movdqu xmm2,[rsp+0x40]
movdqu xmm3,[rsp+0x50]
movdqu xmm4,[rsp+0x60]
movdqu xmm5,[rsp+0x70]
add rsp,0x88
pop r11
pop r10
pop r9
pop r8
pop rdx
pop rcx
pop rax
popfq
jmp qword ptr [rip+native]
blocked:
lock inc dword ptr [rip+denied]
movdqu xmm0,[rsp+0x20]
movdqu xmm1,[rsp+0x30]
movdqu xmm2,[rsp+0x40]
movdqu xmm3,[rsp+0x50]
movdqu xmm4,[rsp+0x60]
movdqu xmm5,[rsp+0x70]
add rsp,0x88
pop r11
pop r10
pop r9
pop r8
pop rdx
pop rcx
pop rax
popfq
ret
native:
.quad 0
seen:
.long 0
denied:
.long 0
