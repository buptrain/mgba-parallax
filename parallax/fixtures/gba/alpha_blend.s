.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA alpha-blend fixture.
 *
 * BG0 is opaque red at priority 1 and target 1. BG1 is opaque blue at
 * priority 2 and target 2. BLDALPHA uses EVA=8 and EVB=8, so every final
 * pixel is the 50/50 GBA blend of BG0 and BG1. Provenance belongs to the
 * front BG0 pixel (source 1, priority 1) and reports alpha blend (1).
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

    /* Mode 0, BG0 and BG1 enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x0300
    strh r1, [r0]

    /* BG0 priority 1, BG1 priority 2; both use 8bpp text tiles. */
    ldr r0, =0x04000008          /* REG_BG0CNT, screen block 30 */
    ldr r1, =0x1E81
    strh r1, [r0]
    ldr r0, =0x0400000A          /* REG_BG1CNT, screen block 31 */
    ldr r1, =0x1F82
    strh r1, [r0]

    /* Palette 1 = red, palette 2 = blue (BGR555). */
    ldr r0, =0x05000002
    ldr r1, =0x001F
    strh r1, [r0]
    ldr r0, =0x05000004
    ldr r1, =0x7C00
    strh r1, [r0]

    /* Tile 0 is opaque red; tile 1 is opaque blue. */
    ldr r0, =0x06000000
    ldr r1, =0x0101
    mov r2, #32
fill_red_tile:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_red_tile
    ldr r1, =0x0202
    mov r2, #32
fill_blue_tile:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_blue_tile

    /* BG0 map selects red tile 0; BG1 map selects blue tile 1. */
    ldr r0, =0x0600F000
    mov r1, #0
    ldr r2, =1024
fill_bg0_map:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_bg0_map
    ldr r0, =0x0600F800
    mov r1, #1
    ldr r2, =1024
fill_bg1_map:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_bg1_map

    /* BG0 target 1, alpha effect, BG1 target 2. */
    ldr r0, =0x04000050          /* REG_BLDCNT */
    ldr r1, =0x0241
    strh r1, [r0]
    ldr r0, =0x04000052          /* REG_BLDALPHA */
    ldr r1, =0x0808              /* EVA=8, EVB=8 */
    strh r1, [r0]

halt:
    b halt

.size _start, . - _start
