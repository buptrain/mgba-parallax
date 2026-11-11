.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA darken-effect fixture.
 *
 * BG0 is opaque red at priority 1 and target 1. Darken mode with EY=8 halves
 * its red channel, yielding the mGBA-resolved RGB 800000. Final provenance remains BG0/source
 * 1, priority 1, and reports darken effect 3.
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

    /* Mode 0 with BG0 enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x0100
    strh r1, [r0]

    /* BG0 priority 1, 8bpp text tiles, screen block 30. */
    ldr r0, =0x04000008          /* REG_BG0CNT */
    ldr r1, =0x1E81
    strh r1, [r0]

    /* Palette 1 = red (BGR555). */
    ldr r0, =0x05000002
    ldr r1, =0x001F
    strh r1, [r0]

    /* Tile 0 is opaque palette index 1. */
    ldr r0, =0x06000000
    ldr r1, =0x0101
    mov r2, #32
fill_red_tile:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_red_tile

    /* Fill BG0's map with tile 0. */
    ldr r0, =0x0600F000
    mov r1, #0
    ldr r2, =1024
fill_bg0_map:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_bg0_map

    /* BG0 target 1, darken effect; EY=8. */
    ldr r0, =0x04000050          /* REG_BLDCNT */
    ldr r1, =0x00C1
    strh r1, [r0]
    ldr r0, =0x04000054          /* REG_BLDY */
    mov r1, #8
    strh r1, [r0]

halt:
    b halt

.size _start, . - _start
