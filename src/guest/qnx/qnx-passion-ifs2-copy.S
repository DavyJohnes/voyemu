// Own Passion B adapter for the pinned startup read callback (W0,X1,W2,W3).
// Restore requested bytes from independent pristine B into the exact A mirror.
// No OEM instruction body, completion word, policy or checksum is substituted.
.text
.p2align 2
.global qnx_passion_ifs2_copy
qnx_passion_ifs2_copy:
    cbz w3, .Lfail
    uxtw x9, w2
    uxtw x10, w3
    // Exact OWN prepared-startup token (raw6720), not a physical GPT LBA.
    mov w12, #0x10000000
    subs x9, x9, x12
    b.lo .Lfail
    add x11, x9, x10
    mov w12, #49055
    cmp x11, x12
    b.hi .Lfail
    lsl x9, x9, #12
    // X4 is only the guarded 3A34 callsite file offset, not a generic UFS ABI.
    uxtw x11, w4
    cmp x9, x11
    b.ne .Lfail
    mov x12, #0xa0000000
    add x13, x12, x9
    cmp x1, x13
    b.ne .Lfail
    mov x12, #0xe5000000
    add x14, x12, x9
    lsl x15, x10, #12
.Lcopy:
    ldp x16, x17, [x14], #16
    stp x16, x17, [x13], #16
    subs x15, x15, #16
    b.ne .Lcopy
    dsb sy
    mov w0, #0
    ret
.Lfail:
    mov w0, #-1
    ret
