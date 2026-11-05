/*
 * glitch_lab — the targets. Easiest first. Each level = one realistic code pattern you will
 * meet in eCTF HSM firmware, wrapped in a trigger window (PB0 high) so you can find it.
 *
 * Every level function is noinline so it shows up as its own symbol in glitchlab.lst; open
 * that listing to map a winning ext_offset back to the exact instruction you hit.
 *
 * Normal (unglitched) behaviour when the host sends the documented WRONG input:
 *   L0 -> A=200         L1..L7 -> DENY         L8 -> DENY n=16
 * Anything else is a fault. GRANT = you won. DETECT = you faulted it but a defense caught you.
 */
#include "lab.h"
#include <string.h>

#define NOINLINE __attribute__((noinline))

/* ---------------------------------------------------------------- secrets */
#define PIN_LEN 6
static const volatile uint8_t  LAB_PIN[PIN_LEN] = { 'c', '0', 'f', 'f', 'e', 'e' };
static const volatile uint32_t L1_PIN = 0x00C0FFEEu;

/* copy up to PIN_LEN chars of the guess, zero-padding a short guess */
static void get_guess(uint8_t *dst, const char *arg)
{
    int ended = 0;
    while (*arg == ' ') arg++;
    for (int i = 0; i < PIN_LEN; i++) {
        if (!ended && arg[i] == '\0') ended = 1;
        dst[i] = ended ? 0 : (uint8_t)arg[i];
    }
}

/* one reply line: "L<n> <RESULT> c=<cycles>" (+ optional " flag=..." on GRANT) */
static void reply(char lvl, uint32_t granted, uint32_t detect, uint32_t cyc, const char *flag)
{
    out_c('L'); out_c(lvl); out_c(' ');
    if (detect)       out_s("DETECT");
    else if (granted) out_s("GRANT");
    else              out_s("DENY");
    out_s(" c="); out_u(cyc);
    if (granted && !detect && flag) { out_s(" flag="); out_s(flag); }
    out_end();
}

/* ======================================================================================
 * L0 — CALIBRATION. 200 fully-unrolled volatile increments. Almost any landed fault changes
 * the count, so this is where you find the energy (repeat/width) that faults this chip at
 * all, before caring about *which* instruction you hit.  Clean = A=200.
 * ==================================================================================== */
#define A1   a += 1;
#define A5   A1 A1 A1 A1 A1
#define A25  A5 A5 A5 A5 A5
#define A100 A25 A25 A25 A25
#define A200 A100 A100

NOINLINE void level0(const char *arg)
{
    (void)arg;
    volatile int a = 0;
    window_open();
    A200
    uint32_t c = window_close();
    out_s("L0 A="); out_u((uint32_t)a); out_s(" c="); out_u(c); out_end();
}

/* ======================================================================================
 * L1 — ONE COMPARE, ONE BRANCH.  `if (guess == PIN) granted = 1;`
 * Compiles to roughly: ldr / cmp / bne skip / movs / str.  Skip or corrupt the bne (or the
 * cmp's flags) and you're in.  Send: "1 0"   (wrong PIN; the real one is 0xC0FFEE)
 * ==================================================================================== */
NOINLINE void level1(const char *arg)
{
    uint32_t guess = parse_hex(arg);
    volatile uint32_t granted = 0;

    window_open();
    if (guess == L1_PIN) {
        granted = 1;
    }
    uint32_t c = window_close();

    reply('1', granted, 0, c, "lab{l1_branch_skip}");
}

/* ======================================================================================
 * L2 — EARLY-EXIT COMPARE (memcmp / strcmp style).  Returns at the first mismatching byte.
 * At -O2 the compiler UNROLLS naive_eq into 5x (ldrb, ldrb, cmp, bne) + a branchless last byte,
 * and sets the "false" return value up front with `movs r0, #0`. Glitch targets:
 *   - skip that `movs r0, #0`: the early `bne` then returns r0 = the guess POINTER (non-zero) -> GRANT
 *   - the caller's `cmp r0, #0` / branch right after `bl naive_eq`
 *   (skipping one `bne` is NOT enough with "000000": byte 2 mismatches again)
 * Bonus lesson: c= grows with the number of correct leading chars -> it is ALSO a timing
 * side channel (`lab_client.py COMx timing2` recovers the PIN with no glitch at all).
 * Send: "2 000000"
 * ==================================================================================== */
static NOINLINE int naive_eq(const uint8_t *a, const volatile uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

NOINLINE void level2(const char *arg)
{
    uint8_t g[PIN_LEN];
    get_guess(g, arg);
    volatile uint32_t granted = 0;

    window_open();
    if (naive_eq(g, LAB_PIN, PIN_LEN)) {
        granted = 1;
    }
    uint32_t c = window_close();

    reply('2', granted, 0, c, "lab{l2_early_exit}");
}

/* ======================================================================================
 * L3 — PERMISSION BIT (eCTF "validate_permission" pattern). You *know* the PIN in this
 * scenario; the only thing between you and the file is one bit test on its permission byte.
 * Slot 0 is readable, slot 1 ("update") is write-only.  Send: "3 1"
 * ==================================================================================== */
#define PERM_R 0x1u
#define PERM_W 0x2u
#define PERM_C 0x4u

static volatile uint8_t file_perms[2] = { PERM_R | PERM_W, PERM_W };
static const char *const file_data[2] = { "hello-public-file", "lab{l3_perm_bit_skip}" };

NOINLINE void level3(const char *arg)
{
    uint32_t slot = parse_dec(arg) & 1u;
    volatile uint32_t granted = 0;

    window_open();
    if (file_perms[slot] & PERM_R) {
        granted = 1;
    }
    uint32_t c = window_close();

    /* on GRANT the "file contents" are the flag */
    reply('3', granted, 0, c, file_data[slot]);
}

/* ======================================================================================
 * L4 — CONSTANT-TIME COMPARE + FINAL BRANCH. No early exit, no timing leak... but:
 *   (a) the final `cmp r0,#0; bne` after `bl ct_diff` is a single point of failure, and
 *   (b) cutting the LOOP short leaves diff = 0 -> GRANT, but ONLY if no mismatching byte was
 *       accumulated yet. With "000000" byte 0 already differs (the loop body always runs once),
 *       so (b) can't work; with "4 c0ffe0" only the last byte differs and (b) becomes the target.
 * Unrolling is disabled so the loop really exists in the binary.  Send: "4 000000"
 * ==================================================================================== */
static NOINLINE uint32_t ct_diff(const uint8_t *a, const volatile uint8_t *b, size_t n)
{
    uint32_t d = 0;
#pragma clang loop unroll(disable)
    for (size_t i = 0; i < n; i++) {
        d |= (uint32_t)(a[i] ^ b[i]);
    }
    return d;
}

NOINLINE void level4(const char *arg)
{
    uint8_t g[PIN_LEN];
    get_guess(g, arg);
    volatile uint32_t granted = 0;

    window_open();
    if (ct_diff(g, LAB_PIN, PIN_LEN) == 0) {
        granted = 1;
    }
    uint32_t c = window_close();

    reply('4', granted, 0, c, "lab{l4_ct_loop_cut}");
}

/* ======================================================================================
 * L5 — REDUNDANT CHECK. The compare runs twice; both must agree. One glitch that flips the
 * first decision lands you in DETECT, not GRANT. To win you need both decisions faulted
 * (two glitches — CW-Husky only), one long glitch spanning both, or a fault that corrupts
 * something BOTH checks depend on (e.g. the guess buffer, or the shared compare result path).
 * Send: "5 000000"
 * ==================================================================================== */
NOINLINE void level5(const char *arg)
{
    uint8_t g[PIN_LEN];
    get_guess(g, arg);
    volatile uint32_t r1, r2;
    volatile uint32_t granted = 0, detect = 0;

    window_open();
    r1 = (ct_diff(g, LAB_PIN, PIN_LEN) == 0);
    r2 = (ct_diff(g, LAB_PIN, PIN_LEN) == 0);
    if (r1) {
        if (r2) granted = 1;
        else    detect  = 1;
    }
    uint32_t c = window_close();

    reply('5', granted, detect, c, "lab{l5_double_trouble}");
}

/* ======================================================================================
 * L6 — FAULT-INJECTION HARDENING (modelled on CMU's 2026 fiproc.h):
 *   - magic success/failure values (zeroing a register gives neither -> DETECT)
 *   - loop-completion check inside the compare
 *   - triple-evaluated condition on a volatile result
 *   - a re-check, then a control-flow counter that must equal 1+2+4
 * The "hard mode". A GRANT needs the result of BOTH fih_eq calls faulted (they are ~100+ cycles
 * apart), i.e. two glitches per trigger (CW-Husky) or a lucky PC corruption. On a CW-Lite the
 * realistic goal is a REPRODUCIBLE DETECT: proof you hit the exact instruction you aimed at.
 * Send: "6 000000"
 * ==================================================================================== */
#define FIH_OK   0x3CA5965Au
#define FIH_FAIL 0xC35A69A5u

static NOINLINE uint32_t fih_eq(const uint8_t *a, const volatile uint8_t *b, size_t n)
{
    volatile uint32_t d = 0;
    volatile size_t i;
    for (i = 0; i < n; i++) {
        d |= (uint32_t)(a[i] ^ b[i]);
    }
    if (i != n) return 0;                    /* loop cut short: neither OK nor FAIL */
    return (d == 0) ? FIH_OK : FIH_FAIL;
}

NOINLINE void level6(const char *arg)
{
    uint8_t g[PIN_LEN];
    get_guess(g, arg);
    volatile uint32_t r = 0, flow = 0;
    volatile uint32_t granted = 0, detect = 0;

    window_open();
    r = fih_eq(g, LAB_PIN, PIN_LEN);
    flow += 1;
    if (r == FIH_OK && r == FIH_OK && r == FIH_OK) {
        flow += 2;
        if (fih_eq(g, LAB_PIN, PIN_LEN) != FIH_OK) {
            detect = 1;
        } else {
            flow += 4;
            if (flow == 7) granted = 1;
            else           detect  = 1;
        }
    } else if (r != FIH_FAIL) {
        detect = 1;                          /* result is garbage -> someone is glitching */
    }
    uint32_t c = window_close();

    reply('6', granted, detect, c, "lab{l6_fih_master}");
}

/* ======================================================================================
 * L7 — RANDOM DELAY (jitter). Same easy single compare as L1, but a random busy-wait sits
 * between the trigger edge and the check, so a fixed ext_offset only lines up sometimes.
 * The reply reports d=<delay loops> so you can correlate hits with the delay (white-box).
 *   "7 0"    -> seeded from host timing (realistic)
 *   "7 0 f"  -> FIXED seed every call (models a badly-seeded RNG: delay is deterministic,
 *               so the "defense" is worthless — try this first)
 * ==================================================================================== */
static uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

NOINLINE void level7(const char *arg)
{
    uint32_t guess = parse_hex(arg);
    const char *sp = strchr(arg, ' ');
    int fixed = (sp && sp[1] == 'f');

    uint32_t seed = fixed ? 0xACE1u : (g_entropy | 1u);
    uint32_t d = xorshift32(&seed) & 0x3FFu;          /* 0..1023 loops ≈ 0..~130 µs */
    volatile uint32_t granted = 0;

    window_open();
    for (volatile uint32_t k = 0; k < d; k++) { }
    if (guess == L1_PIN) {
        granted = 1;
    }
    uint32_t c = window_close();

    out_s("L7 "); out_s(granted ? "GRANT" : "DENY");
    out_s(" c="); out_u(c); out_s(" d="); out_u(d);
    if (granted) out_s(" flag=lab{l7_jitter_beaten}");
    out_end();
}

/* ======================================================================================
 * L8 — BOUNDS CLAMP -> MEMORY OVER-READ (ChipWhisperer "Fault 2_3" pattern).
 * The host asks to dump n bytes of a 16-byte public buffer. The clamp `if (n > 16) n = 16`
 * is the only thing stopping a read into the secret stored right after it.
 * Skip the clamp and the dump runs on into the secret.  Send: "8 32"
 * (The printing loop also stops at 32 bytes so a corrupted n can't spew all of RAM; that
 *  is a lab convenience, not part of the target.)
 * ==================================================================================== */
static volatile struct {
    uint8_t pub[16];
    uint8_t secret[16];
} g_mem = {
    { 'p','u','b','l','i','c','-','d','a','t','a','-','0','1','2','3' },
    { 'l','a','b','{','l','8','_','o','v','e','r','r','e','a','d','}' },
};

NOINLINE void level8(const char *arg)
{
    volatile uint32_t n = parse_dec(arg);

    window_open();
    if (n > 16u) {
        n = 16u;
    }
    uint32_t c = window_close();

    uint32_t shown = (n < 32u) ? n : 32u;
    out_s("L8 "); out_s(shown > 16u ? "GRANT" : "DENY");
    out_s(" c="); out_u(c); out_s(" n="); out_u(n); out_s(" data=");
    const volatile uint8_t *p = g_mem.pub;
    for (uint32_t i = 0; i < shown; i++) out_x8(p[i]);
    out_end();
}

/* ======================================================================================
 * L9 - WIDENED L1. Same single compare + branch as L1, but padded so the branch sits deep
 * inside a LONG trigger window. For boards whose stock 0.47 uF VCORE cap can't be modified:
 * L1's ~18-count window is too short (a pulse long enough to droop the rail overruns it and
 * resets), but a long window lets the long-pulse regime that faults this board (see L0) land
 * on the branch. Attack it like L0: coarse repeat ~100..900, sweep the whole window.
 * Send: "9 0" (wrong PIN; the real one is 0xC0FFEE, same as L1)
 * ==================================================================================== */
#ifndef L9_PAD
#define L9_PAD 60
#endif

NOINLINE void level9(const char *arg)
{
    uint32_t guess = parse_hex(arg);
    volatile uint32_t granted = 0;
    volatile uint32_t sink = 0;

    window_open();
    for (volatile uint32_t i = 0; i < L9_PAD; i++) { sink += i; }   /* lead pad  */
    if (guess == L1_PIN) {
        granted = 1;                                                  /* THE branch */
    }
    for (volatile uint32_t i = 0; i < L9_PAD; i++) { sink += i; }   /* trail pad */
    uint32_t c = window_close();
    (void)sink;

    reply('9', granted, 0, c, "lab{l9_widened_skip}");
}
