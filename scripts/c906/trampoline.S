/* Position-independent C906 code, copied outside the firmware carveout.
 * a0 points to c906_reload_gate. No stack or old-firmware references. */
.section .text,"ax",@progbits
.option norvc
.global c906_reload_trampoline
c906_reload_trampoline:
    csrci mstatus, 8
    csrw mie, zero
    .long 0x0030000b /* dcache.ciall; SDK binutils lacks this mnemonic */
    .long 0x0190000b /* sync.s: finish old-firmware writebacks */
    csrw mhcr, zero /* uncached polling; new _start enables caches again */
    fence.i
    li t0, 1
    sw t0, 4(a0)
    fence iorw, iorw
1:
    lw t0, 4(a0)
    li t1, 2
    bne t0, t1, 1b
    fence iorw, iorw
    fence.i
    csrw mscratch, a0 /* handover page for the new firmware ready signal */
    lwu t0, 8(a0)
    jr t0
