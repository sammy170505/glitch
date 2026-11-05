/*
 * glitch_lab — main loop: line-based UART protocol on the host port + glitch trigger.
 *
 * Protocol (115200 8-N-1, ASCII, one line in -> one line out):
 *   on reset         -> "BOOT glitchlab v1"         (latch guard: a clean reboot prints this)
 *   "v"              -> "glitchlab v1 levels=0-9 trig=PB0"
 *   "p"              -> "PONG"                      (liveness check after a shot)
 *   "<N> [arg]"      -> "L<N> <RESULT> c=<cycles> [extra]"   for N = 0..8
 *   anything else    -> "ERR"
 *
 * RESULT is one of: GRANT (goal met) / DENY (clean, normal refusal) / DETECT (a countermeasure
 * noticed the fault) / A=<n> (level 0 only). No reply at all = crash/reset/latch.
 * See ../README.md for the per-level arguments and what "success" means for each.
 */
#include "lab.h"

volatile uint32_t g_entropy = 0;
volatile uint32_t g_t0 = 0;

/* ---------------------------------------------------------------- UART output */
void out_c(char c) { DL_UART_transmitDataBlocking(HOST_UART, (uint8_t)c); }

void out_s(const char *s) { while (*s) out_c(*s++); }

void out_u(uint32_t v)
{
    char buf[11];
    int i = 0;
    if (v == 0) { out_c('0'); return; }
    while (v) { buf[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i--) out_c(buf[i]);
}

void out_x8(uint8_t v)
{
    static const char hx[] = "0123456789abcdef";
    out_c(hx[v >> 4]);
    out_c(hx[v & 0xF]);
}

void out_end(void) { out_c('\r'); out_c('\n'); }

/* ---------------------------------------------------------------- parsing */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t parse_hex(const char *s)
{
    uint32_t v = 0;
    int d, n = 0;
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (n < 8 && (d = hexval(*s)) >= 0) { v = (v << 4) | (uint32_t)d; s++; n++; }
    return v;
}

uint32_t parse_dec(const char *s)
{
    uint32_t v = 0;
    int n = 0;
    while (*s == ' ') s++;
    while (n < 9 && *s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s - '0'); s++; n++; }
    return v;
}

/* ---------------------------------------------------------------- UART input */
static void read_line(char *buf, size_t n)
{
    size_t i = 0;
    for (;;) {
        while (DL_UART_isRXFIFOEmpty(HOST_UART)) {
            g_entropy++;                    /* host timing jitter -> entropy for L7 */
        }
        char ch = (char)DL_UART_receiveData(HOST_UART);
        if (ch == '\r') continue;
        if (ch == '\n') { buf[i] = '\0'; return; }
        if (i < n - 1) buf[i++] = ch;
    }
}

/* ---------------------------------------------------------------- init */
static void lab_init(void)
{
    SYSCFG_DL_init();                       /* 32 MHz SYSOSC, UART0/UART1, LED on, S2 input */

    /* trigger pin: PB0, push-pull output, idle LOW */
    DL_GPIO_initDigitalOutput(TRIG_IOMUX);
    DL_GPIO_clearPins(TRIG_PORT, TRIG_PIN);
    DL_GPIO_enableOutput(TRIG_PORT, TRIG_PIN);

    /* SysTick: free-running 24-bit down-counter on the CPU clock (cycle-accurate window) */
    SysTick->LOAD = 0x00FFFFFFu;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;   /* no interrupt */

    /* LED D1 (PB14) is left ON by SYSCFG_DL_init and never toggled: LED-off == latch/dead. */
}

int main(void)
{
    char line[48];

    lab_init();
    out_s("BOOT " LAB_VERSION);
    out_end();

    for (;;) {
        read_line(line, sizeof line);
        if (line[0] == '\0') continue;

        const char *arg = (line[1] == ' ') ? &line[2] : &line[1];

        switch (line[0]) {
        case 'v': out_s(LAB_VERSION " levels=0-9 trig=PB0"); out_end(); break;
        case 'p': out_s("PONG"); out_end(); break;
        case '0': level0(arg); break;
        case '1': level1(arg); break;
        case '2': level2(arg); break;
        case '3': level3(arg); break;
        case '4': level4(arg); break;
        case '5': level5(arg); break;
        case '6': level6(arg); break;
        case '7': level7(arg); break;
        case '8': level8(arg); break;
        case '9': level9(arg); break;
        default:  out_s("ERR"); out_end(); break;
        }
    }
}
