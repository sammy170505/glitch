/*
 * MSPM0L2228 voltage-glitch TARGET firmware
 * ------------------------------------------
 * Companion to notebooks/mspm0_glitch.ipynb (ChipWhisperer-Lite driver).
 *
 * What it does, per attempt (host-gated so the CW campaign stays in sync):
 *   1. On reset, prints "BOOT" over UART  -> the notebook's per-reset latch
 *      guard reads this to confirm a clean reboot.
 *   2. Waits for a single command byte over UART:
 *        'g'  run the add-block under a GPIO trigger, report "A=<value>"
 *        't'  SELF-TEST: run the block then force a miscount, report "A=<value>"
 *             (proves the Glitched-detection path end to end -- "positive
 *             control lite"; a TRUE positive control is landing a real fault)
 *        'v'  identify: "MSPM0L2228-GLITCH v1"
 *
 * The glitch TIMING reference is the TRIGGER GPIO rising edge (raised right
 * before the 200 adds, lowered right after) -- NOT the UART edge. UART here is
 * only the go/report channel. This is exactly Astra's "delay + GPIO pulse at
 * the beginning" option, and the notebook sweeps ext_offset across the whole
 * trigger-high window so no single timing region can silently hide faults.
 *
 * ---------------------------------------------------------------------------
 * Pins (confirm against your SYSCONFIG .syscfg -- see README):
 *   TRIGGER out : PB0  (LQFP64 pin 47)  -> wire to ChipWhisperer tio4
 *   UART TX     : PA10 (pin 56)         -> header "TXD" -> XDS110 backchannel RX
 *   UART RX     : PA11 (pin 57)         -> header "RXD" -> XDS110 backchannel TX
 *   Heartbeat   : PB14 (pin 2, LED D1)  -> blinks once per reply (optional)
 * UART: 115200 8-N-1 on UART0. MCLK: 32 MHz (SYSOSC default).
 *
 * Host link = the XDS110's backchannel UART (same USB used to flash) -> UART0 on
 * PA10/PA11, per the eCTF getting-started 8-wire hookup. No separate USB-serial
 * dongle needed. NOTE: the eCTF *challenge* firmware instead talks on UART1
 * (PA8=TX / PA9=RX); if you later glitch that firmware, move the host link there
 * (change GLITCH_UART + the SYSCONFIG UART instance/pins).
 * ---------------------------------------------------------------------------
 */

#include "ti_msp_dl_config.h"   /* SYSCONFIG-generated: SYSCFG_DL_init(), pin/UART defs */
#include "add_block.h"

/* ---- Map these to whatever your SYSCONFIG names/placements are ---------- */
#ifndef TRIG_PORT
#define TRIG_PORT    GPIOB
#endif
#ifndef TRIG_PIN
#define TRIG_PIN     DL_GPIO_PIN_0        /* PB0 */
#endif
#ifndef LED_PORT
#define LED_PORT     GPIOB
#endif
#ifndef LED_PIN
#define LED_PIN      DL_GPIO_PIN_14       /* PB14 / D1 */
#endif
#ifndef GLITCH_UART
#define GLITCH_UART  UART0                /* PA10/PA11 */
#endif
/* ------------------------------------------------------------------------ */

static inline void trig_high(void) { DL_GPIO_setPins(TRIG_PORT, TRIG_PIN); }
static inline void trig_low(void)  { DL_GPIO_clearPins(TRIG_PORT, TRIG_PIN); }

static void uart_putc(char c)
{
    DL_UART_Main_transmitDataBlocking(GLITCH_UART, (uint8_t)c);
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_putdec(int v)
{
    char buf[12];
    int i = 0;
    unsigned u;

    if (v < 0) { uart_putc('-'); u = (unsigned)(-v); }
    else       { u = (unsigned)v; }

    if (u == 0) { uart_putc('0'); return; }
    while (u) { buf[i++] = (char)('0' + (u % 10u)); u /= 10u; }
    while (i--) uart_putc(buf[i]);
}

static uint8_t uart_getc(void)
{
    return DL_UART_Main_receiveDataBlocking(GLITCH_UART);
}

/* The vulnerable region. `a` MUST be volatile (see add_block.h). */
static int run_add_block(void)
{
    volatile int a = 0;
    trig_high();
    A200;                 /* 200x: a += 1;  -- fully unrolled, no loop */
    trig_low();
    return a;
}

static void report(int a)
{
    uart_puts("A=");
    uart_putdec(a);
    uart_puts("\r\n");
    /* heartbeat blink so you can see activity on D1 (optional) */
    DL_GPIO_togglePins(LED_PORT, LED_PIN);
}

int main(void)
{
    SYSCFG_DL_init();     /* clock=32MHz, IOMUX, UART 115200 -- from SYSCONFIG */

    trig_low();
    uart_puts("BOOT\r\n");                 /* reboot marker for the latch guard */

    for (;;) {
        uint8_t cmd = uart_getc();
        switch (cmd) {
        case 'g': {
            int a = run_add_block();
            report(a);                      /* clean => A=200 */
            break;
        }
        case 't': {                         /* detection self-test */
            int a = run_add_block();
            a -= 1;                         /* deliberately wrong -> must read GLITCHED */
            report(a);
            break;
        }
        case 'v':
            uart_puts("MSPM0L2228-GLITCH v1\r\n");
            break;
        default:
            break;                          /* ignore stray bytes / line endings */
        }
    }
}
