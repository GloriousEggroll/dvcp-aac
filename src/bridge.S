// Site ABI is validated by the scanner before this entry can be used.
.text
.hidden aac_qt_entry
.hidden aac_qt_make
.hidden aac_qt_ready
.hidden aac_qt_fallback
.global aac_qt_entry
.type aac_qt_entry,@function
aac_qt_entry:
    cmpl $0x61616320, %r15d
    jne .Loriginal
    movq %r12, %rdi
    movl %ebp, %esi
    call aac_qt_make
    movq %rax, %r13
    testq %rax, %rax
    je .Loriginal
    movq $1, 0x20(%rsp)
    jmp *aac_qt_ready(%rip)
.Loriginal:
    movq (%r12), %rax
    movq %r12, %rbx
    movq %r12, %rdi
    movl %ebp, %esi
    call *0x60(%rax)
    jmp *aac_qt_fallback(%rip)
.size aac_qt_entry,.-aac_qt_entry

// Resolve 21.1.1: reader r13, fourcc r12d, stream rbx, codec r15, flag 0x8(%rsp).
.hidden aac_qt_entry_21_1
.global aac_qt_entry_21_1
.type aac_qt_entry_21_1,@function
aac_qt_entry_21_1:
    cmpl $0x61616320, %r12d
    jne .Loriginal_21_1
    movq %r13, %rdi
    movl %ebp, %esi
    call aac_qt_make
    testq %rax, %rax
    je .Loriginal_21_1
    movq %rax, %r15
    movq $1, 0x8(%rsp)
    jmp *aac_qt_ready(%rip)
.Loriginal_21_1:
    movq (%r13), %rax
    movq %r13, %rdi
    movl %ebp, %esi
    call *0x60(%rax)
    movq %rax, %rbx
    jmp *aac_qt_fallback(%rip)
.size aac_qt_entry_21_1,.-aac_qt_entry_21_1

.hidden aac_mkv_gate
.hidden aac_mkv_probe
.hidden aac_mkv_dispatch
.hidden aac_mkv_config
.hidden aac_stream_make
.hidden aac_mkv_accept
.hidden aac_mkv_gate_fallback
.hidden aac_mkv_probe_fallback
.hidden aac_mkv_finalize
.hidden aac_mkv_other
.hidden aac_mkv_mp3
.hidden aac_mkv_ready
.hidden aac_mkv_failure
.global aac_mkv_gate
.type aac_mkv_gate,@function
aac_mkv_gate:
    jne .Lmkv_not_aac
    movl $1, %eax
    jmp *aac_mkv_accept(%rip)
.Lmkv_not_aac:
    jmp *aac_mkv_gate_fallback(%rip)
.size aac_mkv_gate,.-aac_mkv_gate

.global aac_mkv_probe
.type aac_mkv_probe,@function
aac_mkv_probe:
    cmpl $1, 0x38(%rsp)
    jne .Lmkv_original_probe
    movq %r12, %rdi
    leaq 0x30(%rsp), %rsi
    call aac_mkv_config
    jmp *aac_mkv_finalize(%rip)
.Lmkv_original_probe:
    xorl %ebp, %ebp
    jmp *aac_mkv_probe_fallback(%rip)
.size aac_mkv_probe,.-aac_mkv_probe

.global aac_mkv_dispatch
.type aac_mkv_dispatch,@function
aac_mkv_dispatch:
    cmpl $1, %eax
    jne .Lmkv_original_dispatch
    movq 0x20(%rsp), %rdi
    call aac_stream_make
    testq %rax, %rax
    je .Lmkv_failed
    movq %rax, %r13
    jmp *aac_mkv_ready(%rip)
.Lmkv_failed:
    jmp *aac_mkv_failure(%rip)
.Lmkv_original_dispatch:
    cmpl $2, %eax
    je .Lmkv_mp3
    jmp *aac_mkv_other(%rip)
.Lmkv_mp3:
    jmp *aac_mkv_mp3(%rip)
.size aac_mkv_dispatch,.-aac_mkv_dispatch
.section .note.GNU-stack,"",@progbits
