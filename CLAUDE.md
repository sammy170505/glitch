# CTF_26 — carried-over context

New working folder for eCTF-2026 / CTF work. This file is the **transferable context** from the
previous ChipWhisperer glitching project (`Notes/ai_iot_chain/mnist`, MAX78000 CNN). It is the
target-agnostic half — the rig and the fault-injection method — with the MAX78000-specific parts
deliberately left out. **No assumption is made that this folder is about ML/AI**; the method below
applies to any voltage-glitch target (secure boot, PIN/password check, ROM bootloader, comms parser,
etc.).

> This file lives at `Notes/CTF_26/CLAUDE.md` so Claude Code auto-loads it when working here. Rename
> or trim freely. Verify every hardware fact against the actual board/install before the first shot —
> these were true on the prior rig and things drift.

---

## The rig (carries over as-is)

- **ChipWhisperer-Lite** for glitch generation + trigger. Open the scope only via `cw.scope()` /
  the harness — never as a raw `serial.Serial(...)` on its COM port.
- **CW python env**: `C:\Users\Samruddhi\ChipWhisperer\cwenv\Scripts\python.exe` (chipwhisperer 6.x).
  Anything that needs the live scope is a **Jupyter notebook** by convention; offline analysis is
  plain `.py`.
- **LP FET only, never both crowbars.** `scope.io.glitch_lp = True`, `scope.io.glitch_hp = False`.
  Enabling HP (or both) risks the board — assert LP-on/HP-off on every shot.
- **Trigger**: `ext_single` on a GPIO the firmware toggles around the target window. On the MAX rig
  that pin was P3_1 → CW `tio4`; on a new board pick a free GPIO and wire it to `tio4`, then confirm
  `scope.trigger.triggers`.
- **Clock**: 60 MHz `clkgen`, `glitch.clk_src = "clkgen"` — so `repeat` maps to absolute time.
- **COM ports drift.** Re-check every session with
  `[System.IO.Ports.SerialPort]::GetPortNames()`. A wrong port looks *exactly* like a HANG. On the
  prior rig: FTHR UART on one port (115200 8-N-1), ChipWhisperer on another.

## The MSP-LITO board (if that's the target)

- Board = **MSP-LITO-MSPM0L2228** (Cortex-M0+, no CNN accelerator — a fresh target; the MAX78000
  CNN work does not apply).
- **Crowbar injection point = VCORE decoupling cap `C3` (0.47µF), no series R.** This is the analog
  of VCOREA on the MAX — where you attach the glitch.
- Toolchain is **TI MSPM0 SDK / CCS / arm-none-eabi**, not MaximSDK/WSL. Build steps TBD once the
  challenge firmware is in hand.

---

## The method (target-agnostic — this is the real carry-over)

Learned the hard way on the prior project (a sloppy sweep produced a false "no fault" conclusion
that a rigorous re-run overturned with hundreds of faults). Follow this before ever concluding a
target is unglitchable:

1. **Measure the target window on-chip — never guess.** Add a hardware-timer profiler around the
   region of interest (start a HW timer, read elapsed; `glitch_cycles ≈ µs × clk_MHz`). Do **not**
   use DWT CYCCNT — it reads 0 standalone on these parts. A guessed window is usually several× wrong,
   so every probe misses the phase. If you can't instrument it, measure the trigger width on the
   scope (`scope.adc.trig_count`).
2. **Sweep `repeat` from LOW with fine steps.** Start small, step up to the first non-clean result,
   then step finely across the clean→non-clean edge. Faults usually live well below the reset/latch
   zone — don't jump straight to high energy.
3. **Cover the WHOLE measured window, including EARLY offsets.** Susceptibility is strongly
   offset-dependent; a single mid-window probe misses it. Use **coarse-wide → dense-fine**: sweep the
   full window at a coarse `ext_offset` step to find occupied spans, then re-sweep those spans at a
   fine step, then decode **where those offsets fall in the instruction stream** (disassemble the
   flashed ELF; map offset → instruction/iteration).
4. **Try both crowbar modes if one finds nothing.** `enable_only` (sustained) and `glitch_only`
   (sub-cycle width) have very different thresholds. "No fault in one mode" is not "no fault."
5. **Classify richly, not just pass/fail.** Clean / (effect but goal not met) / (goal met) / Reset /
   Latch. Don't collapse everything to "did the exploit fire" — partial effects are the map to the
   working point.
6. **Onset drifts within a session** (thermal). The energy that works now may not in 30 min. Rescan
   the working point periodically; express results relative to a re-measured onset, not as absolute
   `repeat` numbers.

## Discipline before every sweep (a "gate")

1. **Baseline** — N no-glitch resets (both FETs off), confirm the target's normal output is
   bit-exact and stable. If baseline is unstable, stop — do not sweep.
2. **Positive control** — reproduce a *known* fault (or a firmware self-test that forces the fault
   condition) so you know the rig can actually land a glitch. A clean sweep is only trustworthy after
   a positive control passes.
3. **Per-reset latch guard** — this board family is **latch-prone**: an SCR latch shows as LED-off,
   and **nRST does NOT clear it — only a USB power-cycle does**. Once latched, every later shot reads
   as "Reset" and silently poisons the run. After any Reset, confirm the board reboots clean;
   if not → declare LATCH, halt, power-cycle. Trust "Clean" results over "Reset" counts.

## Logging convention

- Per experiment: `runs/YYYYMMDD_HHMMSS_<target>/` with `run.md` (human record), `diff.patch`
  (firmware change snapshot), `trials.csv` (per-shot data).
- Keep `trials.csv` **lean and flat**: one row per shot, run-constant fields in a `meta.json`,
  and keep the full raw device output in a single `raw` column as the escape hatch so any field can
  be re-mined offline without re-running the board.
- Report every file you add/modify and its purpose when handing work back.

---

## Pointers (source material — do not copy wholesale)

- `Notes/26_ectf/` — prior eCTF-2026 work already on disk: `2026-ectf-secure-code/`,
  `binghamton-ectf-handoff/`, `attack_packages/`, the team writeup tracker.
- `Notes/ai_iot_chain/mnist/CROSS_MODEL_RESULTS.md` — the "three methodology bugs" writeup that this
  method section distills (guessed windows, wrong crowbar mode, sparse/late sweeps).
- `Notes/ai_iot_chain/mnist/` — the full ChipWhisperer harness (`char_target/glitch_harness.py`:
  `setup_scope`, `arm`, `recover_nrst`; notebook cell defining the UART link + `reset_board`).
  Reusable as a starting point; strip the MAX78000/CNN specifics.

## Does NOT transfer (MAX78000-CNN-specific — ignore for a new target)

Weight witness / mask-SRAM readback, mexpress packed-weight loader, VCOREA vs VCOREB retention,
`cnn.c` / `cnn_load_weights`, the weight-load corruption results, class-flip / stuck-at-zero
findings, MaximSDK + WSL build. None of it applies to a Cortex-M0+ with no accelerator.
