// qnx-ipl-virt.S: initial program loader at 0x40200000; the last QEMU loader, with cpu-num=0,
// loads it and sets the PC here.
// QEMU bare-metal boot leaves the EL1 stage-1 MMU off (SCTLR_EL1=0x00c50838, M=0), so all memory
// is Device-nGnRnE and an unaligned ldrh in the OEM startup faults. With identity 1 GiB blocks the
// loader maps the first GiB (MMIO) as Device and the RAM with the guest images as Normal
// non-cacheable, which permits unaligned accesses, then enables the EL1 MMU and jumps to the OEM
// startup entry 0x85700800. The base 0x40200000 stays clear of the DTB at 0x40000000-0x40100000,
// which a base of 0x40010000 would overlap. The block table lives at L0_BASE 0x40300000.

// T0SZ=25 makes the walk start at level 1, see _start; with another start level QEMU reads the
// table at the wrong level and reports a bogus level 2 permission fault once the OEM startup
// builds its own mappings.

#ifndef QNX_STARTUP_ENTRY
#define QNX_STARTUP_ENTRY 0x85700800
#endif
#ifndef QNX_STARTUP_STATUS
#define QNX_STARTUP_STATUS 0x85750000
#endif

// Four GiB remains the byte-identical default; larger mapping is adapter opt-in.
#ifndef QNX_IPL_RAM_GIB
#define QNX_IPL_RAM_GIB 4
#endif
#if QNX_IPL_RAM_GIB != 4 && QNX_IPL_RAM_GIB != 6
#error unsupported QNX_IPL_RAM_GIB
#endif

#define L0_BASE 0x40300000
#define L1_BASE 0x40301000
#define MMIO_BASE 0x08000000
// Level 1 block descriptor: [1:0]=01 block (0b11 is a table), [4:2]=AttrIdx, [5]=NS,
// [7:6]=AP 00 (EL1 read-write), [9:8]=SH 11 (inner shareable), [10]=AF 1, [11]=nG 0.
// MAIR index 0 is Device (0x00), index 1 Normal non-cacheable (0x44). Device blocks also set
// PXN (53) and XN (54), so nothing executes from MMIO.
#define TT_DEVICE  (0x0701 | (1 << 54) | (1 << 53))   // AttrIdx 0, Device
#define TT_NORMAL  (0x0705)                            // AttrIdx 1, Normal non-cacheable

.globl _start
_start:
    // T0SZ=25 gives a 39-bit VA, so the walk starts at level 1: TTBR0 points straight at a
    // table of 512 1 GiB blocks indexed by VA[38:30]. The output address of a block sits in
    // descriptor bits [47:30].
    ldr  x0, =L0_BASE
    ldr  x1, =TT_DEVICE                 // [0] 0x00000000-0x3fffffff MMIO
    str  x1, [x0]
    ldr  x2, =(0x40000000 | TT_NORMAL)   // [1] RAM
    str  x2, [x0, #8]
    ldr  x2, =(0x80000000 | TT_NORMAL)   // [2] RAM: startup, imagefs, IFS2 copy 0xb0000000
    str  x2, [x0, #16]
    ldr  x2, =(0xc0000000 | TT_NORMAL)   // [3] RAM
    str  x2, [x0, #24]
#if QNX_IPL_RAM_GIB == 6
    ldr  x2, =(0x100000000 | TT_NORMAL)
    str  x2, [x0, #32]
    ldr  x2, =(0x140000000 | TT_NORMAL)
    str  x2, [x0, #40]
#endif

    // MAIR: index 0 Device 0x00, index 1 Normal non-cacheable 0x44
    ldr  x0, =0x00004400
    msr  mair_el1, x0

    // TTBR0 = block table, inner shareable; TTBR1 unused
    ldr  x0, =(L0_BASE | 0x2)
    isb
    msr  ttbr0_el1, x0
    msr  ttbr1_el1, xzr

    // TCR: TG0 4 KiB granule, T0SZ=25 (39-bit VA, walk from level 1), IPS 0b010 (40-bit PA),
    // SH0 3 (inner shareable), ORGN0/IRGN0 01 (write-back)
    ldr  x0, =((0x2 << 32) | (0x0 << 14) | (0x3 << 12) | (0x1 << 10) | (0x1 << 8) | 25)
    msr  tcr_el1, x0
    isb

    // Invalidate the TLB and enable the MMU
    tlbi vmalle1
    dsb  sy
    mrs  x0, sctlr_el1
    orr  x0, x0, #0x1
    msr  sctlr_el1, x0
    isb

    // The startup kprintf reads its ring buffer descriptor at [0x85750008], its sink buffer at
    // [0x85750010] and the sink size at [0x85750018]; with a NULL sink printing livelocks. Both
    // buffers lie in the RAM bank at 0x50000000, inside the console ring 0x50010000..0x50030000
    // that the host reads.
    ldr  x0, =QNX_STARTUP_STATUS
    ldr  x1, =0x50020000
    str  x1, [x0, #0x8]
    ldr  x1, =0x50021000
    str  x1, [x0, #0x10]
    ldr  x1, =0x4000
    str  x1, [x0, #0x18]

    ldr  x0, =QNX_STARTUP_ENTRY
    br   x0
