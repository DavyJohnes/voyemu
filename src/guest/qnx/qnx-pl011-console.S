.text
.balign 4
.global qnx_pl011_tx
qnx_pl011_tx:
 movz x7, #0
 movk x7, #0, lsl #16
 movk x7, #0, lsl #32
 movk x7, #0, lsl #48
.Ltx_common:
 mov w8, #0x301
 str w8, [x7, #0x30]
.Ltx_wait:
 ldr w8, [x7, #0x18]
 tbnz w8, #5, .Ltx_wait
 str w1, [x7]
 ret
.global qnx_pl011_early
qnx_pl011_early:
 uxtb w1, w0
 movz x7, #0x900, lsl #16
 b .Ltx_common
.global qnx_pl011_init
qnx_pl011_init:
 cmp w0, #1
 b.hi .Linit_return
 adr x9, qnx_debug_device_records
 add x9, x9, w0, uxtw #4
 movz x8, #0x900, lsl #16
 str x8, [x9]
.Linit_return:
 ret
.global qnx_pl011_probe
qnx_pl011_probe:
 movz x7, #0x900, lsl #16
 ldr w8, [x7, #0x18]
 tbnz w8, #4, .Lprobe_return
 ldr w8, [x7]
 cmp w8, #0x71
 b.ne .Lprobe_return
 b qnx_existing_boot_entry
.Lprobe_return:
 ret
.space 112-(.-qnx_pl011_tx), 0
.set qnx_debug_device_records, qnx_pl011_tx+(0x857655f8-0x857017cc)
.set qnx_existing_boot_entry, qnx_pl011_tx+(0x85703710-0x857017cc)
