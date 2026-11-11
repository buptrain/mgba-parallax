.syntax unified
.cpu arm7tdmi
.arm

/*
 * Synthetic GBA resolved-frame fixture.
 *
 * It selects Mode 3 (240x160 16-bit bitmap), writes a 16-pixel checkerboard,
 * then loops forever. It deliberately needs neither a BIOS nor game assets.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

start:
    ldr r0, =0x04000000          /* REG_DISPCNT */
    ldr r1, =0x00000403          /* Mode 3 + BG2 */
    strh r1, [r0]

    ldr r0, =0x06000000          /* VRAM */
    mov r1, #0                   /* y */
row:
    mov r2, #0                   /* x */
    mov r3, r1, lsr #4
column:
    mov r4, r2, lsr #4
    eor r4, r4, r3
    tst r4, #1
    ldreq r5, =0x7FFF            /* white, BGR555 */
    ldrne r5, =0x001F            /* red, BGR555 */
    strh r5, [r0], #2
    add r2, r2, #1
    cmp r2, #240
    blt column
    add r1, r1, #1
    cmp r1, #160
    blt row

halt:
    b halt

.size _start, . - _start
