.syntax unified
.cpu arm7tdmi
.arm

/*
 * Native parity fixture. The Mode 3 top-left 16 x 16 patch starts cyan.
 * Exactly once per VBlank it samples GBA A and toggles cyan/magenta on an A
 * rising edge. Both latch and stage are held in emulated EWRAM, not CPU
 * registers, so a native save/load checkpoint has deterministic state.
 *
 * This intentionally uses no DMA, IRQ, BIOS, or external data. In particular
 * it avoids a free-running CPU-speed input loop, which made a one-frame host
 * input script nondeterministic between frontends.
 */
.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    b start
    .space 0xBC

.equ REG_DISPCNT,  0x04000000
.equ REG_VCOUNT,   0x04000006
.equ REG_KEYINPUT, 0x04000130
.equ EWRAM_STATE,  0x02000000
.equ VRAM,         0x06000000

start:
    ldr r0, =REG_DISPCNT
    ldr r1, =0x00000080          /* forced blank while VRAM is initialized */
    strh r1, [r0]

    /* Clear the full 240 x 160 Mode 3 surface, not only the observed patch.
     * A resolved-frame hash includes VRAM power-up pixels, which differ across
     * fresh direct and libretro sessions. Eight words per iteration keeps boot
     * deterministic without DMA, IRQ, BIOS, or external data. */
    ldr r0, =VRAM
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r4, #0
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0
    ldr r9, =2400                /* (240 * 160 * 2) / 32 */
clear_vram:
    stmia r0!, {r1-r8}
    subs r9, r9, #1
    bne clear_vram

    ldr r0, =REG_DISPCNT
    ldr r1, =0x00000403          /* Mode 3 + BG2 */
    strh r1, [r0]

    ldr r4, =EWRAM_STATE
    mov r1, #0
    str r1, [r4]                 /* [0]: stage (0 cyan, 1 magenta) */
    str r1, [r4, #4]             /* [4]: previous A state */

frame:
    /* Synchronize to the start of one VBlank. The input state is then held
     * for one emulator frame by the runner before the next scripted mask. */
    ldr r0, =REG_VCOUNT
wait_not_vblank:
    ldrh r1, [r0]
    cmp r1, #160
    bhs wait_not_vblank
wait_vblank:
    ldrh r1, [r0]
    cmp r1, #160
    blo wait_vblank

    ldr r0, =REG_KEYINPUT        /* KEYINPUT is active low */
    ldrh r1, [r0]
    tst r1, #1                   /* GBA A */
    moveq r2, #1
    movne r2, #0
    ldr r3, [r4, #4]
    cmp r2, r3
    str r2, [r4, #4]
    beq draw
    cmp r2, #0
    beq draw
    ldr r3, [r4]
    eor r3, r3, #1
    str r3, [r4]

draw:
    ldr r3, [r4]
    tst r3, #1
    ldreq r1, =0x7FE0            /* cyan BGR555 */
    ldrne r1, =0x7C1F            /* magenta BGR555 */
    ldr r0, =VRAM
    mov r2, #256                 /* 16 x 16 pixels */
fill:
    strh r1, [r0], #2
    subs r2, r2, #1
    bne fill
    b frame

.size _start, . - _start
