// Own developer adapter for absent physical boot-storage query objects.
// Link this separate section at0x8a000080, preserving the first115restore bytes.
.section .boot_storage,"ax",@progbits
.p2align 6
.global qnx_boot_storage_secondary
qnx_boot_storage_secondary:
    cbz x0, .Lsecondary_missing
    ldr x9, qnx_boot_storage_context
    ldr x9, [x9]
    cbz x9, .Lsecondary_missing
    ldr x9, [x9, #0x2088]
    cbz x9, .Lsecondary_missing
    ldr x9, qnx_boot_storage_secondary_callback
    br x9
.Lsecondary_missing:
    mov w0, #6
    ret

.p2align 6
.global qnx_boot_storage_primary
qnx_boot_storage_primary:
    cbz x0, .Lprimary_missing
    ldr x9, qnx_boot_storage_context
    ldr x9, [x9]
    cbz x9, .Lprimary_missing
    ldr x9, [x9, #0x2080]
    cbz x9, .Lprimary_missing
    ldr x9, qnx_boot_storage_primary_callback
    br x9
.Lprimary_missing:
    mov w0, #6
    ret

.balign 8
qnx_boot_storage_context:
    .quad 0x857632e8
qnx_boot_storage_secondary_callback:
    .quad 0x85710310
qnx_boot_storage_primary_callback:
    .quad 0x85710258
