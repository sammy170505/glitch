/*
 * glitch_lab — deliberately-insecure MSPM0L2228 practice target for voltage glitching.
 *
 * Shared definitions for main.c (UART protocol + trigger) and levels.c (the targets).
 * NOT a real HSM. Every "secret" here is meant to be stolen with a glitch.
 */
#ifndef LAB_H
#define LAB_H

#include <stdint.h>
#include <stddef.h>
#include "ti_msp_dl_config.h"   /* TI-generated: UART0 on PA10/PA11 @115200, LED PB14, S2 PB21 */

#define LAB_VERSION "glitchlab v1"

/* ---- Trigger: PB0 = U1 pin 47 -> header J1 inner column, row 4 (PA5 above, PA7 below) -> CW tio4 */
#define TRIG_PORT   GPIOB
#define TRIG_PIN    DL_GPIO_PIN_0
#define TRIG_IOMUX  IOMUX_PINCM12

/* ---- Host link: UART0 = XDS110 backchannel COM port (same port the eCTF tools use) ----- */
#define HOST_UART   UART_0_INST

/* ---- UART output helpers (main.c) ---------------------------------------------------- */
void out_c(char c);
void out_s(const char *s);
void out_u(uint32_t v);         /* unsigned decimal */
void out_x8(uint8_t v);         /* two hex digits   */
void out_end(void);             /* "\r\n"           */

/* ---- Argument parsing helpers (main.c) ----------------------------------------------- */
uint32_t parse_hex(const char *s);
uint32_t parse_dec(const char *s);

/* Free-running counter bumped while idle waiting for UART bytes -> cheap entropy for L7. */
extern volatile uint32_t g_entropy;

/*
 * The glitch WINDOW. window_open() raises PB0 (the ChipWhisperer trigger) right before the
 * protected check; window_close() drops it right after and returns the CPU cycles spent
 * inside, measured with SysTick (24-bit down-counter on the 32 MHz core clock).
 * Every reply carries this as c=<cycles>, so you always know the on-chip window length
 * (CLAUDE.md method step 1: measure the window, never guess). DWT CYCCNT does not exist on
 * Cortex-M0+, which is why SysTick is used.
 */
extern volatile uint32_t g_t0;

static inline __attribute__((always_inline)) void window_open(void)
{
    g_t0 = SysTick->VAL;
    DL_GPIO_setPins(TRIG_PORT, TRIG_PIN);
}

static inline __attribute__((always_inline)) uint32_t window_close(void)
{
    DL_GPIO_clearPins(TRIG_PORT, TRIG_PIN);
    return (g_t0 - SysTick->VAL) & 0x00FFFFFFu;
}

/* ---- The levels (levels.c). Each prints exactly one reply line. ---------------------- */
void level0(const char *arg);   /* calibration: 200 unrolled adds          */
void level1(const char *arg);   /* single 32-bit compare + branch          */
void level2(const char *arg);   /* early-exit byte compare (memcmp-style)  */
void level3(const char *arg);   /* permission-bit check (eCTF-style)       */
void level4(const char *arg);   /* constant-time compare + final branch    */
void level5(const char *arg);   /* redundant (double) check                */
void level6(const char *arg);   /* FIH: magic values + triple check + CFI  */
void level7(const char *arg);   /* random delay before a simple compare    */
void level8(const char *arg);   /* bounds clamp -> memory over-read        */
void level9(const char *arg);   /* L1 compare padded into a long window    */

#endif /* LAB_H */
