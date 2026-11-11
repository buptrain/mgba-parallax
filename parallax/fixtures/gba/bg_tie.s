.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA provenance fixture: equal-priority text backgrounds.
 *
 * BG0 and BG1 both use priority 2. BG0 is opaque cyan over the left 120
 * pixels and transparent on the right; BG1 is opaque magenta everywhere.
 * The expected final result is cyan/BG0 on the left (the documented BG0 over
 * BG1 equal-priority tie break) and magenta/BG1 on the right. The right half
 * proves BG1 is enabled rather than merely absent from the tie region.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

start:
    /* Mode 0, BG0 and BG1 enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x0300
    strh r1, [r0]

    /* Both backgrounds: priority 2, 8bpp, character base 0. */
    ldr r0, =0x04000008          /* REG_BG0CNT, screen base block 30 */
    ldr r1, =0x1E82
    strh r1, [r0]
    ldr r0, =0x0400000A          /* REG_BG1CNT, screen base block 31 */
    ldr r1, =0x1F82
    strh r1, [r0]

    /* BG palette 1 = cyan; palette 2 = magenta (BGR555). */
    ldr r0, =0x05000002
    ldr r1, =0x7FE0
    strh r1, [r0]
    ldr r0, =0x05000004
    ldr r1, =0x7C1F
    strh r1, [r0]

    /* Tile 0 is cyan, tile 1 is magenta, tile 2 is transparent. */
    ldr r0, =0x06000000
    mov r1, #1
    mov r2, #64
fill_cyan_tile:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne fill_cyan_tile
    mov r1, #2
    mov r2, #64
fill_magenta_tile:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne fill_magenta_tile
    mov r1, #0
    mov r2, #64
fill_transparent_tile:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne fill_transparent_tile

    /* BG0 map: cyan tile through column 14; transparent tile thereafter. */
    ldr r0, =0x0600F000
    mov r2, #32                  /* map rows */
bg0_row:
    mov r3, #0                   /* tile column */
bg0_column:
    cmp r3, #15
    movlt r1, #0
    movge r1, #2
    strh r1, [r0], #2
    add r3, r3, #1
    cmp r3, #32
    blt bg0_column
    subs r2, r2, #1
    bne bg0_row

    /* BG1 map: magenta tile across the whole map. */
    ldr r0, =0x0600F800
    mov r1, #1
    ldr r2, =1024
bg1_map:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne bg1_map

halt:
    b halt

.size _start, . - _start
