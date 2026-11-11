.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA provenance fixture: OBJ/BG priority tie.
 *
 * BG0 is opaque cyan at priority 2. A normal 8bpp 64 x 64 OBJ at (0,0)
 * is opaque magenta and also priority 2. GBA OBJ wins a BG priority tie, so
 * the upper-left 64 x 64 square must report OBJ/source 5; all other pixels
 * must remain BG0/source 1. The sprite is deliberately not blended.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

start:
    /* OAM is unavailable during active scanout on real hardware. Configure
     * every video memory region during VBlank so this fixture tests the PPU,
     * not a timing accident in one emulator implementation. */
    ldr r0, =0x04000006          /* REG_VCOUNT */
wait_vblank:
    ldrh r1, [r0]
    cmp r1, #160
    bne wait_vblank

    /* Mode 0, one-dimensional OBJ mapping, BG0 and OBJ enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x1140
    strh r1, [r0]

    /* BG0: priority 2, 8bpp, character base 0, screen base block 31. */
    ldr r0, =0x04000008          /* REG_BG0CNT */
    ldr r1, =0x1F82
    strh r1, [r0]

    /* BG palette entry 1 = cyan; OBJ palette entry 2 = magenta. */
    ldr r0, =0x05000002
    ldr r1, =0x7FE0
    strh r1, [r0]
    ldr r0, =0x05000204
    ldr r1, =0x7C1F
    strh r1, [r0]

    /* BG tile 0: opaque cyan. */
    ldr r0, =0x06000000
    mov r1, #1
    mov r2, #64
fill_bg_tile:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne fill_bg_tile

    /* 64 x 64 OBJ at tile 0 in OBJ VRAM: 64 contiguous 8bpp tiles. */
    ldr r0, =0x06010000
    ldr r1, =0x0202              /* OBJ VRAM rejects byte writes. */
    ldr r2, =2048
fill_obj_tiles:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_obj_tiles

    /* BG0 map: tile 0 across the 32 x 32 text map. */
    ldr r0, =0x0600F800
    mov r1, #0
    ldr r2, =1024
fill_bg_map:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_bg_map

    /* Hide every OBJ before defining sprite zero. */
    ldr r0, =0x07000000
    mov r1, #160
    mov r2, #128
hide_obj:
    strh r1, [r0]
    add r0, r0, #8
    subs r2, r2, #1
    bne hide_obj

    /* OBJ 0: y=0, 8bpp square; x=0, 64 x 64; tile=0, priority=2. */
    ldr r0, =0x07000000
    ldr r1, =0x2000              /* attr0 */
    strh r1, [r0]
    ldr r1, =0xC000              /* attr1 */
    strh r1, [r0, #2]
    ldr r1, =0x0800              /* attr2 */
    strh r1, [r0, #4]

halt:
    b halt

.size _start, . - _start
