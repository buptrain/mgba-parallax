.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA Mode 2 affine-BG2 identity-transform fixture.
 *
 * BG2 uses one opaque cyan 8bpp affine tile at priority 2. PA=PD=1.0 and
 * PB=PC=0 establish an identity transform. A 32x32 map covers the full GBA
 * width. Final provenance is BG2/source 3.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

start:
    /* Program display memory in VBlank for hardware-valid behavior. */
    ldr r0, =0x04000006          /* REG_VCOUNT */
wait_vblank:
    ldrh r1, [r0]
    cmp r1, #160
    bne wait_vblank

    /* Mode 2 with affine BG2 enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x0402
    strh r1, [r0]

    /* BG2 priority 2, character block 0, screen block 30, 32x32 map. */
    ldr r0, =0x0400000C          /* REG_BG2CNT */
    ldr r1, =0x5E02
    strh r1, [r0]

    /* Identity affine matrix: PA=PD=1.0, PB=PC=0. */
    ldr r0, =0x04000020          /* REG_BG2PA */
    ldr r1, =0x0100
    strh r1, [r0]
    ldr r0, =0x04000022          /* REG_BG2PB */
    mov r1, #0
    strh r1, [r0]
    ldr r0, =0x04000024          /* REG_BG2PC */
    strh r1, [r0]
    ldr r0, =0x04000026          /* REG_BG2PD */
    ldr r1, =0x0100
    strh r1, [r0]
    ldr r0, =0x04000028          /* REG_BG2X */
    mov r1, #0
    str r1, [r0]
    ldr r0, =0x0400002C          /* REG_BG2Y */
    str r1, [r0]

    /* Palette 1 = cyan (BGR555). */
    ldr r0, =0x05000002
    ldr r1, =0x7FE0
    strh r1, [r0]

    /* Affine tile 0 is opaque palette index 1. */
    ldr r0, =0x06000000
    mov r1, #1
    mov r2, #64
fill_cyan_tile:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne fill_cyan_tile

    /* The 32x32 affine map selects tile 0 everywhere. */
    ldr r0, =0x0600F000
    mov r1, #0
    ldr r2, =1024
fill_bg2_map:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne fill_bg2_map

halt:
    b halt

.size _start, . - _start
