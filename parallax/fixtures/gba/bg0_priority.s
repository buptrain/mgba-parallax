.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA provenance fixture: a fully opaque Mode 0 BG0.
 *
 * BG0 uses 8bpp text mode, priority 2, tile zero, and palette entry one
 * (cyan). Every visible pixel must therefore resolve to BG0, priority 2,
 * with no blending. This deliberately exercises the renderer's text-BG
 * source bookkeeping rather than the Mode 3 BG2 bitmap path.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

start:
    /* Mode 0, BG0 enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    mov r1, #0x100
    strh r1, [r0]

    /* BG0: priority 2, 8bpp, character base 0, screen base block 31. */
    ldr r0, =0x04000008          /* REG_BG0CNT */
    ldr r1, =0x1F82
    strh r1, [r0]

    /* BG palette entry 1 = cyan in BGR555. Palette entry 0 remains the
     * backdrop/transparent value, so every tile byte of 1 is opaque BG0. */
    ldr r0, =0x05000002
    ldr r1, =0x7FE0
    strh r1, [r0]

    /* Tile 0: 8 x 8 pixels, all palette index 1. */
    ldr r0, =0x06000000
    mov r1, #1
    mov r2, #64
tile_fill:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne tile_fill

    /* 32 x 32 text map entries, each selecting tile 0. */
    ldr r0, =0x0600F800
    mov r1, #0
    ldr r2, =1024
map_fill:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne map_fill

halt:
    b halt

.size _start, . - _start
