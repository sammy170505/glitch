# glitch_lab — practice target for voltage glitching the MSP-LITO-L2228

A deliberately insecure firmware with **9 levels**, from "can I fault this chip at all" up to
CMU-style fault-injection hardening. Each level is one code pattern you'll meet in eCTF HSM
firmware, wrapped in a GPIO trigger window. Work through them on a **Design-Phase (un-keyed)**
board before touching any attack package.

```
glitch_lab/
  build.ps1  flash.ps1          build in Docker (build-hsm image) / flash over UART
  firmware/  src/main.c          UART protocol, PB0 trigger, SysTick cycle counter
             src/levels.c        the 9 targets (read this file first: each level is documented)
             inc/lab.h           pins + window_open()/window_close()
             firmware.ld         linked at 0x6000 (behind MITRE's insecure bootloader)
             firmware_bare.ld    linked at 0x0    (no bootloader, flash over SWD)
             (ti_msp_dl_config.*, startup_*.c, status_led.h copied unchanged from the 2026 insecure example)
  host/      lab_client.py       serial client + classifier + `selftest` gate (no CW needed)
             glitch_lab.ipynb    ChipWhisperer-Lite sweeps, latch guard, runs/ logging
  build/     outputs: glitchlab_<layout>.elf/.bin/.map and .lst (disassembly with source)
```

---

## About "the debugger is disabled"

That applies only to the **keyed Attack-Phase boards**: MITRE's secure bootloader locks SWD.
On a **Design-Phase board, SWD over the XDS110 works**. You need it once, to install the insecure
bootloader. After that you never need the debugger: you **flash over UART** through the
bootloader, and the **glitch attack only uses UART + one trigger wire + NRST + the crowbar**,
the same as against a locked board later.

**Never Uniflash-erase a keyed board.** Its secure bootloader can't be restored.

---

## Phase 0 — one-time: install the insecure bootloader (Design-Phase board)

Image: `C:\Users\Samruddhi\Downloads\2026.0.2.insecure.out` (also in `26_ectf/attack_packages/du/du_repo/insecure.out`).

1. Wire the XDS110 to J3 with all **8 jumpers**, labels matched: 3V3, GND, SWDIO, SWCLK, RXD(PA11), TXD(PA10), NRST, BSL.
   A wrong pin can damage the board.
2. Open **UniFlash 9.7** (`C:\ti\uniflash_9.7.0`). Device **MSPM0L2228**, connection **XDS110**, then Start.
3. **Settings & Utilities** → erase setting **"Erase MAIN and NONMAIN necessary sectors only"** (MITRE's instruction).
4. Program → browse to the `insecure.out` → **Load Image**.
5. Check: hold **S2**, tap **S1** (reset), release S2. **D1 flashes red** = the bootloader is in update mode.

## Phase 1 — build, flash, sanity-check (no ChipWhisperer yet)

```powershell
cd C:\Users\Samruddhi\Desktop\Notes\CTF_26\glitch_lab
.\build.ps1                                   # Docker build-hsm image -> build\glitchlab_bootloader.bin
[System.IO.Ports.SerialPort]::GetPortNames()  # find the XDS110 backchannel port (ports drift!)
# board into update mode: hold S2, tap S1, release S2  (D1 flashing red)
.\flash.ps1 -Port COM7                        # = uvx ectf hw COM7 reflash ... (erase+flash+start)
C:\Users\Samruddhi\ChipWhisperer\cwenv\Scripts\python.exe host\lab_client.py COM7 selftest
```

`selftest` is the **baseline + positive-control gate**. Every level must be STABLE on the wrong
input (same result and same cycle count `c=` 20/20), and the right input must GRANT. Expect
`GATE: PASS`. You can also poke it by hand: `lab_client.py COM7 send "1 0"` → `L1 DENY c=…`.

**Fallback, Option B (bare metal, no bootloader).** Use it if the bootloader causes trouble, e.g. boot is
too slow or it doesn't auto-start the app after NRST. Run `.\build.ps1 -Layout bare`, then flash
`build\glitchlab_bare.elf` with UniFlash, with erase set to **MAIN only** (leave NONMAIN alone).
This wipes the insecure bootloader; redo Phase 0 to get it back.

## Phase 2 — hardware: cap mod + wiring

- **Cap mod** (details in [../glitch_mspm0/firmware/README.md](../glitch_mspm0/firmware/README.md)): replace **C3**
  (0.47 µF on VCORE, beside U1's top-right corner) with ≈480 nF in series with ≈36 Ω. Crowbar goes on
  the VCORE side. Never run with no cap. Do this on a **spare Design-Phase board**.
- Wiring. Start from the eCTF 8-wire XDS110 hookup on J3 (the one from the eCTF website). For glitching,
  **remove 4 of those jumpers and keep 4**, then add the ChipWhisperer wires.

**XDS110 → J3 (the eCTF 8-wire hookup):**

| J3 pin | During glitching | Why |
|---|---|---|
| 3V3 | **keep** | the XDS110 powers the board |
| GND | **keep** | |
| RXD (PA11) | **keep** | the serial link to the PC (UART0, the COM port). `lab_client.py`, the notebook and `flash.ps1` all talk through it |
| TXD (PA10) | **keep** | same serial link, other direction |
| NRST | **remove** | the ChipWhisperer drives reset now. Two drivers on one reset line fight each other |
| SWDIO | **remove** | debugger lines, not needed after Phase 0 |
| SWCLK | **remove** | |
| BSL | **remove** | stops the XDS110 from accidentally invoking TI's ROM bootloader |

**ChipWhisperer-Lite → board (added):**

| Board | CW-Lite | Note |
|---|---|---|
| **PB0** (J1 = left header block, inner column, row 4: between PA5 above and PA7 below) | `tio4` | trigger; firmware drives it high only around the check |
| VCORE (old C3 pad) | glitch out, **LP only** | never HP |
| **NRST** (J3 NRST pin you just freed, or J2 NRST; same net) | `nRST` | CW owns reset |
| GND (any GND pin) | GND | common ground for board, XDS110 and CW |

Re-flashing later doesn't need the removed jumpers: `flash.ps1` goes over the kept RXD/TXD. Enter update
mode by hand (hold S2, tap S1, release S2). The 4 removed jumpers only go back for Phase 0 (UniFlash over SWD).

## Phase 3 — the levels (do them in this order)

**Step-by-step for each level: [LEVELS.md](LEVELS.md)** (where to aim, sweep settings, done-criteria, progress log).

Open `host\glitch_lab.ipynb` (cwenv kernel), set `TARGET_PORT` and `LEVEL`, and run top to bottom.
It runs the gate (baseline → positive control → window measurement), then sweeps
`ext_offset` × `repeat` and writes `runs\<ts>_glitchlab_L<n>_<mode>\` (`meta.json`, `trials.csv`,
`run.md`). It then prints an offset × repeat map: `.` clean, `e` effect, `D` detect, `G` grant, `R` no reply.

| Order | Level | Send (wrong input) | Win = | What it teaches | eCTF analog |
|---|---|---|---|---|---|
| 1 | **L0** add-block | `0` | `A≠200` | find the **energy** (repeat/width) that faults the chip at all; map reset/latch zone | — |
| 2 | **L1** single compare | `1 0` | `GRANT` | skip one `bne`. Window is ~5 instructions; see below | `if (pin_ok)` |
| 3 | **L3** permission bit | `3 1` | `GRANT` (prints the file) | same fault model, eCTF-shaped | `validate_permission()` (Read Update flag) |
| 4 | **L4** constant-time compare | `4 000000` | `GRANT` | **cut the loop short** → `diff` stays 0; or hit the final branch | CT `memcmp` PIN/HMAC checks |
| 5 | **L2** early-exit compare | `2 000000` | `GRANT` | loop/return glitching. **Bonus:** `c=` grows per correct leading char, a timing leak with no glitch needed | naive `memcmp` / `strcmp` |
| 6 | **L8** clamp → over-read | `8 32` | `GRANT`, `data=` contains `lab{l8_overread}` | skip a bounds clamp to **leak memory** (key extraction) | length checks before UART dumps |
| 7 | **L7** random delay | `7 0 f` then `7 0` | `GRANT` | `f` = fixed seed: the delay is deterministic, so the "defense" is useless. Without `f`, jitter: correlate hits with `d=`, accept a lower hit rate | `random_delay()` (cedarville, cmu) |
| 8 | **L5** double check | `5 000000` | `GRANT` (single fault → `DETECT`) | why redundancy works. Try one long glitch across both decisions. CW-Lite can't do 2 glitches/trigger (`num_glitches` is Husky-only) | binghamton/byu double-reads |
| 9 | **L6** FIH hardened | `6 000000` | `GRANT` | magic values, loop-completion check, triple-check, CFI counter. Mostly `DETECT`; a GRANT here means you've mastered it | CMU `fiproc.h` |

**Mapping a hit to an instruction (do this every level).** Open `build\glitchlab_bootloader.lst` and find
`<levelN>`. `window_open()` is the `str` to the GPIO DOUTSET register (`0x400a3290`). Example, level 1 as built:

```
74a4: str  r2, [r5]          ; PB0 HIGH  <- trigger edge (cycle 0)
74a6: ldr  r3, [pc, #0x34]   ; &L1_PIN
74a8: ldr  r3, [r3]          ; load the real PIN
74aa: cmp  r0, r3
74ac: bne  0x74b0            ; <- THE target: skip/corrupt this -> granted = 1
74ae: str  r2, [sp, #0x4]    ; granted = 1
74b0: str  r2, [r5, #0x10]   ; PB0 LOW (DOUTCLR)
```

1 target cycle (31.25 ns) ≈ 1.9 CW counts. A hot `ext_offset` × 32/60 ≈ target cycles after the edge.
Add a few cycles for GPIO/CW latency, and expect ±1-cycle smear because the MSPM0 SYSOSC is not
synced to the CW clock.

**Method reminders (from `../CLAUDE.md`):** sweep `repeat` from LOW; cover the whole window plus
margin, coarse then fine; try **both** `enable_only` and `glitch_only`; re-measure onset every ~30 min.
On LED-off, note that **a HardFault also turns D1 off** (`Default_Handler` in the startup file), and NRST
clears it. A true SCR **latch survives NRST** and needs a USB power-cycle. The notebook's latch guard
tells them apart by whether `BOOT` comes back.

## Phase 4 — graduate (then the attack packages)

1. **Lose the GPIO.** Real targets won't toggle PB0 for you. Wire the host→board UART line (PA11/RXD)
   to `tio4` and trigger off the end of the command you send. Re-find L1/L3 with only that reference
   and calibrate against the PB0 result. UART idles high, so check the trigger polarity on the bench.
2. **White-box twin of a real design.** Build our own 2026 HSM (binghamton) on a Design-Phase board,
   then glitch its PIN check and `validate_permission`.
3. Then a hardened one (cmu). Only after that, the attack packages.

---

### Not yet verified on hardware (check on first bring-up)
- That the insecure bootloader **auto-starts the app after NRST** without S2. If it waits for `uvx ectf hw COMx start`,
  use Option B (bare) for glitching.
- Boot time to the `BOOT` banner behind the bootloader. Raise `Lab(boot_timeout=…)` if needed.
- PB0 confirmed free in the schematic (net PB.0 = U1 pin 47 + header only; R1–R4 are on the PA3–PA6 crystal nets). The notebook's window cell still
  confirms it: `trigger seen: True` with a sensible width.
- Timing figures (`c=` per level) are from the build, not measured yet. `selftest` prints the real ones.
