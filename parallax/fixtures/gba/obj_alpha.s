.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA semi-transparent OBJ fixture.
 *
 * An opaque cyan BG0 and a semi-transparent magenta 8bpp OBJ share priority
 * 2. OBJ covers the upper-left 64x64 pixels. With EVA=EVB=8 it resolves to
 * RGB 7F7FFF in that square and retains OBJ/source-5 provenance.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

start:
    /* Configure video memory during VBlank. */
    ldr r0, =0x04000006          /* REG_VCOUNT */
wait_vblank:
    ldrh r1, [r0]
    cmp r1, #160
    bne wait_vblank

    /* Mode 0, one-dimensional OBJ mapping, BG0 and OBJ enabled. */
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x1140
    strh r1, [r0]

    /* BG0: priority 2, 8bpp, character block 0, screen block 31. */
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

    /* 64x64 OBJ tile data: opaque palette index 2. OBJ VRAM uses halfwords. */
    ldr r0, =0x06010000
    ldr r1, =0x0202
    ldr r2, =2048
fill_obj_tiles:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill_obj_tiles

    /* BG0 map: tile 0 across the 32x32 text map. */
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

    /* OBJ 0: semi-transparent, y=0, 8bpp square; x=0, 64x64, priority 2. */
    ldr r0, =0x07000000
    ldr r1, =0x2400              /* attr0: semitransparent + 8bpp */
    strh r1, [r0]
    ldr r1, =0xC000              /* attr1 */
    strh r1, [r0, #2]
    ldr r1, =0x0800              /* attr2 */
    strh r1, [r0, #4]

    /* OBJ target 1, alpha effect, BG0 target 2; EVA=8, EVB=8. */
    ldr r0, =0x04000050          /* REG_BLDCNT */
    ldr r1, =0x0150
    strh r1, [r0]
    ldr r0, =0x04000052          /* REG_BLDALPHA */
    ldr r1, =0x0808
    strh r1, [r0]

halt:
    b halt

.size _start, . - _start
