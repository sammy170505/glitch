# glitch_lab — how to beat every level

Prereqs: Phases 0–2 of [README.md](README.md) done (bootloader, flash, `selftest` PASS, cap mod, wiring).
All work happens in `host\glitch_lab.ipynb`. Read the level's block in
[firmware/src/levels.c](firmware/src/levels.c) and its disassembly in `build\glitchlab_bootloader.lst` before you start.

---

## The loop (same for every level)

| Step | Notebook cell | What you do |
|---|---|---|
| 1. Gate | "Gate" | Set `LEVEL` (and `SEND` for variants). Baseline must be STABLE, positive control GRANT. Note `c_fw` (window in target cycles) and `OFFSET_MAX` |
| 2. Aim | — | In the `.lst`, find `<levelN>`. Locate the trigger `str` (to `0x400a3290`) and the target instruction. Count cycles between them |
| 3. Sweep | "Sweep" + "Map" | Coarse first (whole window), then `OFFSETS = near_end(...)` or a hand range at step 1. `REPEATS` = the band you found in L0 |
| 4. Read the map | "Map" | `G` = win. `e`/`D` = you're hitting *something*: move the offset. All `.` → more energy. All `R` → less energy |
| 5. Confirm | "Confirm" | 100 shots at the best point → GRANT rate. Re-confirm after 30 min (thermal drift). Write it in `run.md` |

**Units.** 1 target cycle = 31.25 ns ≈ **1.875 CW counts** (`ext_offset`, `repeat`).
The cycle numbers below are **nominal Cortex-M0+** (LDR/STR 2, taken branch 2, BL 3). Flash wait states
at 32 MHz can stretch them, so scale with the measured `c=`, not the nominal sum.

**Latency.** Your hit offset = (target cycle × 1.875) + a **fixed rig latency** (GPIO → CW trigger → FET → VCORE).
Level 1 measures that latency for you. Write it down and use it to *predict* every later level.

**Common trick:** in L1, L3, L4, L2 (caller) and L8, the deciding branch sits **2–6 cycles before the window closes**
(the window ends with `str granted` then the GPIO-clear `str`). `near_end(before_cycles=12, after_cycles=4)` aims there.

---

## L0 — calibration: find the energy  (`0`)

- **Window:** 200 × (`ldr`, `adds`, `str`) on a stack variable, ≈1000 cycles ≈ 31 µs. Every offset inside is equivalent.
- **Sweep:** `OFFSETS = range(200, OFFSET_MAX-200, 200)` (a few mid-window points), `REPEATS = range(1, 41)`, `TRIES = 5`.
- **Read:** the lowest `repeat` with any `e` = **onset**. The lowest with mostly `R` = **reset zone**. Between = your
  **working band** (often just a few counts wide).
- **Then:** re-run with `MODE = 'glitch_only'`, `REPEATS = range(1, 6)`, sweeping `WIDTH` in 5 % steps (10…45). It has a
  different threshold; keep whichever mode gives the wider band.
- **Done when:** `e` rate ≥ 10 % at one repeat, and you've written down `onset`, `reset`, mode and time.
  **No `e` at all, even up to reset?** Check the crowbar is on the VCORE side of the cap mod, then the cap value.

## L1 — skip one branch  (`1 0`)

```
str  r2,[r5]      ; trigger edge, cycle 0
ldr  r3,=L1_PIN   ; 2
ldr  r3,[r3]      ; 2
cmp  r0,r3        ; 1
bne  skip         ; <- TARGET (~cycle 5-6)
str  r2,[sp,#4]   ; granted = 1
str  r2,[r5,#16]  ; trigger low
```
- **Sweep:** whole window + 20 at step 1 (≈35 offsets), `REPEATS` = onset-1 … reset-1, `TRIES = 3`.
- **Expect:** a narrow `G` column. Neighbours `e`/`R`.
- **Calibrate:** `latency_counts = hit_offset - 5.5 × 1.875`. Write it down.
- **Done when:** GRANT ≥ 5 % at one (offset, repeat), and it still works 30 min later.

## L3 — permission bit  (`3 1`)

Same shape as L1: `ldr` address → `ldrb perms[slot]` → `lsls #31` (bit 0 into flags) → **`beq` skip** → `str granted`.
- **Exercise:** **predict** the offset from your L1 latency *before* sweeping. Then sweep ±10 counts to check.
- **Done when:** GRANT prints `flag=lab{l3_perm_bit_skip}` at the predicted offset (±3). This is the
  eCTF *Read Update* pattern.

## L8 — skip a bounds clamp → leak memory  (`8 32`)

```
str  r1,[r5]      ; trigger
ldr  r2,[sp,#8]   ; n = 32
cmp  r2,#17
blo  ok           ; not taken for n=32
movs r2,#16       ; <- skip this...
str  r2,[sp,#8]   ; <- ...or this, and n stays 32
ok: str r1,[r5,#16]
```
- **Sweep:** whole window, step 1 (it's tiny).
- **Win:** `L8 GRANT n=32 data=…`. The second 16 bytes decode to the secret:
  `bytes.fromhex(data)[16:]` → `b'lab{l8_overread}'`.
- **Lesson:** this is how a glitch **extracts keys** (feeds the eCTF *Compromise / Backdoor* flags).

## L4 — constant-time compare: hit the final branch  (`4 000000`)

- **Window:** `bl ct_diff` (6-iteration loop, ≈90–110 cycles) → `cmp r0,#0` → **`bne`** → `str granted` → trigger low.
- **Aim:** the `bne` is the last decision, ~4 cycles before the end → `OFFSETS = near_end(12, 4)`, step 1.
- **Why not "cut the loop"?** With `000000` the first iteration already ORs in a mismatch (the body always runs once),
  so an early exit still leaves `diff ≠ 0`.
- **Variant 4b — loop cut:** `SEND = '4 c0ffe0'` (only the LAST byte wrong). Now make the loop exit before
  iteration 6. The target is the loop's back-edge `bne` (`ct_diff+0x16`) on its 5th pass, ~20–30 cycles before the
  end → `near_end(40, 0)`. **Lesson:** a loop-cut works only when the mismatch is late, i.e. it depends on the input.
- **Done when:** GRANT on 4, and a GRANT on 4b at a *different* offset than 4.

## L2 — early-exit compare  (`2 000000`)

The compiler unrolled the loop: `movs r0,#0` (default "false"), then 5× (`ldrb`, `ldrb`, `cmp`, `bne` → return) and a
branchless last byte. Back in the caller: `cmp r0,#0` → **`beq`** → `str granted`.
- **Target A (early):** skip `movs r0,#0` inside `naive_eq` (~10–14 cycles after the edge). The first `bne` then
  returns `r0` = the guess **pointer** (non-zero) → GRANT. A non-obvious single fault: the "default false" was the weak spot.
- **Target B (late):** the caller's `beq` → `near_end(12, 4)`.
- **Skipping one `bne` is not enough:** byte 2 mismatches again.
- **Side quest (no glitch):** `lab_client.py COM7 timing2` recovers `c0ffee` from `c=` alone, because each correct
  leading char adds ~6 cycles. Real attackers would measure response time or power instead of `c=`.
- **Done when:** GRANTs at both A and B (two separate hot offsets on the map), and `timing2` prints the PIN.

## L7 — random delay  (`7 0 f`, then `7 0`)

- **Fixed seed first** (`SEND = '7 0 f'`): the delay is always `d=975` loops (~10–11 cycles each), so the window is
  ~10k+ cycles ≈ 20k CW counts, but **constant**. The compare is L1's, at the end → `near_end(12, 4)` at step 1.
  Coarse-sweeping the whole window is a waste here. **Lesson:** a bad seed makes the "defense" worthless.
- **Then random** (`SEND = '7 0'`): keep the fixed-seed winning offset and run **Confirm** with `CONF_N = 3000`.
  That offset only lines up when `d == 975`, so expect GRANT rate ≈ (your L1 rate) / 1024. The `d` histogram of
  hits will show 975 (±1).
- **Lesson:** jitter costs you ~1000× in shots. In eCTF, beat it by triggering **after** the delay: a power-trace feature
  (CW-Lite ADC to find it; CW-Husky can trigger on it), or a later observable event. Also check whether the RNG seed is
  predictable.
- **Done when:** fixed-seed GRANT ≥ 5 %, and at least one random-mode GRANT with `d` = 975 ± 1.

## L5 — redundant check  (`5 000000`)

```
bl ct_diff ; r1 = (diff==0)        bl ct_diff ; r2 = (diff==0)
ldr r0,[sp,#16] ; cmp #0 ; beq out      <- decision 1
ldr r0,[sp,#12] ; cmp #0 ; beq detect   <- decision 2
str granted ...
```
- **Step 1:** fault decision 1 only (~8–10 cycles before the end) → you should get **`D` (DETECT)**, not `G`. That
  proves your aim, and shows why redundancy works.
- **Step 2 (CW-Lite):** one **long** `enable_only` glitch spanning both decisions (~8 cycles ≈ 15–19 counts). Push
  `repeat` toward the reset zone at offsets just before decision 1. Expect mostly `R`/`D`. Any `G` is a real achievement.
- **(CW-Husky:** `num_glitches = 2`, one per decision.)
- **Done when:** a reproducible `D` band, plus your best long-glitch result recorded (even if 0 GRANT).

## L6 — FIH hardened  (`6 000000`)

- **Map reading is the skill here.** Skipping one of the three `bne`s in the triple check → the next one catches it,
  the result is still `FIH_FAIL` → **DENY with a different `c`** → `e`. Faulting *inside* `fih_eq` (cut the loop, or skip
  the `ldr =FIH_FAIL`) returns a value that's neither OK nor FAIL → **`D`**.
- A GRANT needs **both** `fih_eq` calls faulted (~100+ cycles apart). That takes two glitches per trigger (Husky), or a
  lucky PC corruption. **Not expected on a CW-Lite.**
- **Done when:** your map shows `e` and `D` bands and you can name the instruction behind each. That's exactly the
  analysis you'll do on a hardened eCTF design (CMU, UL) to decide if it's worth attacking.

---

## Progress log (fill in as you go)

| Level | Date | Mode / width | Working repeat band | Best (offset, repeat) | GRANT rate | Notes |
|---|---|---|---|---|---|---|
| L0 | | | | | n/a | onset = , reset = |
| L1 | | | | | | latency_counts = |
| L3 | | | | predicted: / actual: | | |
| L8 | | | | | | |
| L4 / 4b | | | | | | |
| L2 A / B | | | | | | timing2: |
| L7 fixed / rand | | | | | | |
| L5 | | | | | D rate: | long-glitch best: |
| L6 | | | | | D rate: | |

**Graduation:** L1/L3/L8 ≥ 5 % on demand, L4 + L2 GRANT, L7 random-mode hit, L5/L6 maps explained. Then move on to
README Phase 4 (UART-edge trigger, then a 2026 design twin).
