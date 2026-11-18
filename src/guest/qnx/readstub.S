.text
.global readstub
readstub:
    // args: w0=handle(junk), x1=dst, w2=block, w3=nblocks
    stp x29, x30, [sp, #-0x40]!
    mov x29, sp
    stp x19, x20, [sp, #0x10]
    stp x21, x22, [sp, #0x20]
    mov x19, x1                 // dst
    lsl x21, x3, #12            // disk blocks are 4 KiB
    lsl x9, x2, #12             // absolute slba * 4096
    movz x5, #0xaffc, lsl #16  // B - partition LBA 64 * 4096
    tbnz w2, #31, fail          // reject negative GPT LBA before mmap
    add x5, x5, x9
    // mmap(0, len, PROT 0xb00, FLAGS 0x10001, fd -1, off phys)
    mov x0, #0
    mov x1, x21
    mov w2, #0xb00
    orr w3, wzr, #0x10001        // 0x10001: MAP_PHYS|MAP_SHARED (movz dropped bit0)
    mov w4, #-1
    bl mmap                     // patched to target 0xc490 later -> assembler needs absolute; use bl with relocation handled manually after
    cmn x0, #1
    b.eq fail
    mov x20, x0                 // src VA
    mov x0, #0
loop:
    ldr x2, [x20, x0]
    str x2, [x19, x0]
    add x0, x0, #8
    cmp x0, x21
    b.lt loop
success:
    mov w0, #0
    ldp x19, x20, [sp, #0x10]
    ldp x21, x22, [sp, #0x20]
    ldp x29, x30, [sp], #0x40
    ret
fail:
    mov w0, #-1
    ldp x19, x20, [sp, #0x10]
    ldp x21, x22, [sp, #0x20]
    ldp x29, x30, [sp], #0x40
    ret
