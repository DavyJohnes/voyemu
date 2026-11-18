// qnx-ifs2-restore.S: helper stubs at 0x8a000000 that let the OEM QNX startup load IFS2 under QEMU.
//
// Entry 0 (offset 0, called from 0x85702cf4 by the patch virt-ifs2-restore) reports a successful
// disk read: the QEMU loaders have already placed the data in RAM.
//
// ifs2_as_entry (called with bl from 0x857006d4 by the patch add-ifs2-as) registers the asinfo
// entry "ifs2_guest" with add_asinfo(start, end, prio, name, owner) at 0x85705790, taking the name
// from the stub's own ifs2_guest_name and owner 0xffff as for "memory", then jumps to the original
// IFS2 loader at 0x85702b40. The entry covers the pristine copy B at 0xb0000000..0xb937bfff:
// another startup writer overwrites the staging copy A, while B stays intact. The restore of the
// first 16 KiB of A from B below writes only A and leaves B unchanged.
//
// The bl leaves the return address into the startup, 0x857006d8, in x30, and blr add_asinfo
// overwrites it, so the loader's ret would come back here and run the loader again forever. The
// stub keeps x30 in x16 from entry and restores it before br to the loader, so the loader's ret
// continues the startup at 0x857006d8.
#ifndef QNX_IFS2_END
#define QNX_IFS2_END 0xb937bfff
#endif
#ifndef QNX_ADD_ASINFO
#define QNX_ADD_ASINFO 0x85705790
#endif
#ifndef QNX_IFS2_LOADER
#define QNX_IFS2_LOADER 0x85702b40
#endif

.globl _start
_start:
    // Entry 0: the data is already in RAM; the SA8155 destination arithmetic would point into
    // QEMU MMIO.
    mov w0, #0
    ret

    .balign 4
    .global ifs2_as_entry
ifs2_as_entry:
    // x16 survives both add_asinfo and the loader.
    mov x16, x30

    // Restore the overwritten first 16 KiB of IFS2 in A from the pristine B.
    ldr x0, =0xa0000000
    ldr x1, =0xb0000000
    mov x2, #0x400
1:  ldp x3, x4, [x1], #16
    stp x3, x4, [x0], #16
    subs x2, x2, #1
    b.ne 1b

    ldr x0, =0xb0000000        // start: pristine B
    // In the 6.0.3 startup add_asinfo takes the inclusive end of the range as its second argument:
    // 0x85705790 stores x23 at [x1,#8], and the OEM call sites 0x8570731c and 0x85707354 pass
    // start+size-1. The guest lookup at 0x286f4..0x2870c computes len=uint32(end-start), so the
    // end 0xb937bfff gives len 0x0937bfff, while the size 0x0937c000 would give 0x5937c000
    // (0x0937c000-0xb0000000 modulo 2^32).
    ldr x1, =QNX_IFS2_END         // end: 0xb0000000 + 0x937c000 - 1
    mov w2, #0                  // prio and flags
    // The address of the string, not its bytes: `ldr x3, label` would load its first eight bytes
    // as a pointer, and the name interner at 0x8570baa0 would fault reading through it.
    adr x3, ifs2_guest_name      // 0x8a000068, NUL-terminated "ifs2_guest"
    mov w4, #0xffff             // owner, as for "memory"
    ldr x5, =QNX_ADD_ASINFO
    blr x5

    // The loader's ret continues the startup.
    mov x30, x16
    ldr x5, =QNX_IFS2_LOADER
    br  x5

    .pool

    // A name distinct from the early FDT entry "ifs2": the first-match lookup at 0x28680 selects an
    // entry by name, and the first startupmgr entry, through the guest patch at 0xef88 (add x0,x0,
    // #0xb0 to #0xb5), points at the string ifs2_guest at 0x2c0b5, which names the entry of the
    // pristine B. The string lives in .text of the stub, at 0x8a000000 plus the label offset.
    .balign 8
    .global ifs2_guest_name
ifs2_guest_name:
    .asciz "ifs2_guest"
