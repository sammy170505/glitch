# Hardware fault attacks on MSP-LITO-L2228 — eCTF 2027 prep primer

Compiled 2026-11-05 from web research + our own 2026 material on disk. Everything about 2027 is
an assumption (same board, similar shape of challenge) — re-read the 2027 rules the day they drop.

---

## 0. Bottom line

1. **No one has published MSPM0 glitch results.** Nothing public on MSPM0 voltage FI, and the
   public 2026 attack write-ups (Purdue repo, etc.) are 100% software/crypto/memory-corruption.
   Teams that show up with a working MSPM0 glitch setup will have points almost no one else can get.
2. **TI ships no fault-injection hardening on MSPM0.** TI's MSPM0 security-enablers app note lists
   "boot configuration routine fault injection attack countermeasures" as **Future** for both the L
   and G families. The only defenses are the ones the team wrote into their own firmware.
3. **The 2026 scenario gave attackers unrestricted physical access to an HSM whose PIN they did
   not know** (the Engineer HSM). That is a textbook glitch target: the PIN-compare branch.
4. **Scope trap:** MITRE's *secure bootloader* on the keyed Attack boards is explicitly **out of
   scope** (Rule 6.2). Attacking it can disqualify you. Glitch the **team's firmware**, not the
   bootloader, the SWD lock it sets up, or NONMAIN/BSL. Ask the organizers before anything borderline.
5. **The attack you can actually win with is "white-box twin"**: every design's source is handed
   over. Build the target's firmware yourself onto an *un-keyed* board, find the glitch point with
   full visibility, then transfer the timing to the keyed board running the real image.

---

## 1. What the attacker had in eCTF 2026 (template for 2027)

Source: rules.ectf.mitre.org/2026 (flags, security reqs, system overview, bootloader pages).

**System:** a file-storage/transfer **HSM**. Management interface = host UART (PIN-protected for
most actions). Transfer interface = **UART1, PA8 TX / PA9 RX** between HSMs. Per-group
permissions: read / write / receive.

**Attack scenario (3 devices):**

| Device | Attacker knows PIN? | Access | Glitchable by you? |
|---|---|---|---|
| Technician HSM | **yes** | full physical | **yes** — permission checks are the target |
| Engineer HSM | no | **unrestricted physical** | **yes** — PIN check is the target |
| PLM (photolithography machine) | no | remote transfer-interface only | **no** — not on your bench |

**Flags and whether a glitch helps:**

| Flag | Goal | Glitch angle |
|---|---|---|
| `steal_*` Steal Design | get a file onto Technician it lacks *receive* perm for | glitch the **receive-permission check** on the Technician, or the **send-side perm check** on the Engineer |
| `update_*` Read Update | read an update file on Technician without *read* perm | **best glitch target.** You know the PIN, so the only thing in the way is the read-permission check |
| `design_*` Read Design | read a design on Engineer without its PIN | glitch the **PIN compare** on the Engineer |
| `compromise_*` Compromise Machine | PLM accepts a corrupted update | PLM is remote. A glitch can only help indirectly (e.g. to extract signing/MAC keys from a board you hold) |
| `backdoor_*` Backdoored Design | PLM accepts the backdoored design | same as above: glitch to **leak keys**, then forge in software |

**Hardware the team got:** 3× un-keyed boards (Design Phase), 3× **keyed** boards (Attack Phase,
carry MITRE's secure bootloader + load `.prot` images), 4× XDS110-ETP-EVM, jumper wires.
**Do not factory-reset a keyed board through Uniflash.** MITRE warns the secure bootloader cannot be
restored afterwards.

**Rules that shape a glitch attack:**
- **Rule 9:** designs may not brick themselves on attack detection, but **may impose a ~5 s
  penalty delay**, and may keep it across restarts. Budget shots per hour accordingly (see §5).
- **Rule 10:** designs must work on *unmodified* hardware, but **you may modify your own
  boards** for attack development. Cap mods on boards you own are fair game.
- **Rule 4:** all source + docs are shared at Handoff, so you get the white-box twin.
- **Rule 7:** every flag needs a write-up of how you got it. Keep `runs/` logs clean (CLAUDE.md
  convention) so the write-up is easy.
- **Open questions to email ectf@mitre.org early in 2027:** (a) is voltage FI on the team firmware
  in scope (physical attacks are in principle); (b) is modifying the VCORE cap on a *keyed*
  board OK; (c) which NONMAIN/SWD settings are the bootloader's (and therefore off-limits).

---

## 2. The MSPM0L2228 as a glitch target

| Fact | Why it matters |
|---|---|
| Arm **Cortex-M0+**, 2-stage pipeline, up to 32 MHz | Simple pipeline gives the classic **instruction-skip / corrupted-branch** fault model. One 32 MHz cycle ≈ 31 ns ≈ 1.9 CW-clkgen counts at 60 MHz |
| Core runs from an **internal LDO on VCORE**, decoupled by **0.47 µF** (board **C3**, VCORE = U1 pin 32, no series R) | That pad is the crowbar injection point. The 0.47 µF tank absorbs short glitches, which is why the cap mod (below) matters |
| **BOR** supply monitor on VDD with 3 programmable thresholds; **parity-protected trim → POR** if corrupted | Too much glitch energy gives a reset, not a fault. Expect a narrow clean→fault→reset band. The BOR watches VDD, so a VCORE glitch may slip under it (verify on the bench) |
| SYSOSC 32 MHz internal, ±1.2 % | The target **self-clocks** and is *not* synced to the CW. Timing jitter is about ±1 cycle per µs of offset, so long offsets smear. Prefer triggers close to the target instruction |
| SWD / BSL policy lives in **NONMAIN** (BOOTCFG0 @ 0x41C00004 SWD policy, BOOTCFG1 BSL pin policy); bad CRC ⇒ **permanent lock** | **Never write NONMAIN on a board you care about.** On keyed boards this is the MITRE bootloader's area: out of scope |
| No TI FI countermeasures (per TI app note) | Whatever you meet is software hardening by the team, so read their source |

**Board mod ("Astra's" recipe, already written up in [glitch_mspm0/firmware/README.md](glitch_mspm0/firmware/README.md)):**
replace C3 with ≈ 480 nF in series with ≈ 36 Ω, crowbar on the VCORE side. A slightly smaller single
cap also works. **Never run with no cap** (the chip dies in boot). Working widths ≈ 1–3 target
cycles (31–94 ns). Keep the pulse under ~20 cycles (~625 ns). **LP FET only, never HP.**

---

## 3. Where the glitch goes in a typical eCTF HSM firmware

Rank targets by **(value of flag) × (how bare the check is)**:

1. **`validate_permission()`-style bit tests** on the Technician (Read Update / Steal Design). These
   are usually a single `ldrb` + `tst` + `beq`, which is the ideal one-instruction skip. You know
   the PIN, so each shot is cheap and no penalty delay applies.
2. **PIN compare** on the Engineer (Read Design). Usually `hash(PIN‖salt)` then `memcmp`/CT-compare
   then a branch. Glitch the **final branch** or the **return-value test**, not the hash. Faulting
   the hash just gives a wrong hash, which then fails. A **wrong PIN costs the penalty delay**, so
   find the timing on the twin first.
3. **Loop-bound / length corruption** (CW "Fault 2_3, dump memory" pattern). Glitch a `memcpy`
   length or a UART-print loop so it over-reads into key material. This feeds Compromise and
   Backdoor: leak the transfer/MAC key, then forge in software.
4. **MAC/signature verify result** on a board you hold. Useful only if the forged object is
   consumed on that same board.

**How to find the instruction:** `arm-none-eabi-objdump -d` the twin ELF, locate the check, and
note the cycles from a known external edge (end of the UART command, or a GPIO you add **only in
the twin**). Then follow CLAUDE.md §method: measure the window, sweep `repeat` from low, cover
the whole window coarse→fine, and try both `enable_only` and `glitch_only`.

---

## 4. Defenses you will meet (seen in the 2026 attack packages on disk)

A rough keyword grep over `26_ectf/attack_packages/` (not yet a hand audit) shows most teams with
only 0–2 incidental hits, which is likely little or no real hardening. The clearly hardened ones are
**ul** and **cmu** (`fiproc.h`), then **era**, **osu**, **utd**, **byu**, **mich**, **dsu**,
**etsu**, **trento**. Patterns seen:

| Pattern | Example | How to approach it |
|---|---|---|
| Triple-evaluated condition | cmu `MULTI_IF_FAILOUT(c)` = `c && c && c` | A single glitch skips one eval. Hit the **code that consumes the result after** the `if`, or the shared load feeding all three. **Multi-glitch is CW-Husky-only** (`num_glitches` raises on Lite, checked in our install), so on a Lite try one *longer* sustained glitch across the cluster |
| Magic success values | cmu `FIH_SUCCESS 0x1AAAAAAA` / `FAIL 0x15555555` | Defeats "zero the register" faults. Target the branch, not the value |
| Control-flow counters | cmu `FI_FLOW_*`, cedarville `PIN_PATH_CP1..3` XOR | Skip the **verify** itself, or the `HALT_AND_CATCH_FIRE` branch (it's a branch too) |
| Random delays | cmu (2k–130k cycles), cedarville `random_delay()` | Destroys fixed-offset timing. Either take a **power trace** with the CW-Lite ADC to spot the check and its jitter, or spray a fixed offset and accept a low hit rate. Check how the RNG is seeded: a constant or reset-predictable seed gives a deterministic delay |
| Double-read + compiler barrier | binghamton perms `v1`/`v2` | Two loads, one compare: glitch the compare/branch |
| Loop-iteration check | cedarville `iterations != len` | Shows that loop-skip was anticipated. Target elsewhere |

**Triage script idea:** grep each package for `glitch|fault|random_delay|MULTI_IF|FIH_|CP1` and sort
teams into soft vs hardened. Glitch the soft ones first (most flags per hour).

---

## 5. Practical constraints

- **Triggering on the real (keyed) board:** you can't add a GPIO to someone else's image. Options:
  (a) put the **host→target UART line** (the bytes *you* send) on `tio4` and trigger at the end
  of the command. UART idles high, so confirm the trigger edge polarity on the bench before
  relying on it. (b) Use a **CW-Lite ADC power trace** (shunt on VDD) to locate the check
  relative to that edge. The Lite has no SAD/pattern trigger (that's Husky), so you trigger on the
  UART edge and use the trace for **offset**. Fix the offset on the **twin** (where you *can*
  add a GPIO), then confirm it against the UART-edge reference.
- **Throughput:** reset + boot + command + (on failure) the up-to-5 s penalty puts you at roughly
  10–20 shots/min at best. Sweep on the twin, where your own build has the penalty removed, and
  reserve the keyed board for a narrow confirmed window.
- **Latch:** this board family can SCR-latch. Only a USB power-cycle clears it. Keep the per-reset
  latch guard (CLAUDE.md gate #3). A USB-switchable hub or a relay on the XDS110 3V3 lets you
  power-cycle automatically.
- **Thermal drift:** re-measure onset every ~30 min (CLAUDE.md §6).
- **NRST contention:** CW and XDS110 both want NRST. After flashing, hand NRST to the CW (see the
  glitch_mspm0 README).

---

## 6. Training plan, now → Attack Phase (~Feb 2027)

| # | Drill | Done when |
|---|---|---|
| 0 | Cap mod on a **spare un-keyed** board. Flash [glitch_mspm0](glitch_mspm0/) add-block target. Baseline gate | `A=200` 30/30 |
| 1 | First real fault: sweep `repeat` low→high, full-window offsets, both crowbar modes | Reproducible `A≠200`. Map offset → instruction from the objdump |
| 2 | **Password-check drill** (port CW "Fault 2_2" to MSPM0): `if (memcmp(pw,ref,n)==0) grant();` | Bypass rate > 1 % at a known offset |
| 3 | **UART-edge trigger** instead of GPIO on the same drill | Same hit, triggered only from host TX |
| 4 | **Our own 2026 HSM** (binghamton) PIN check + `validate_permission`, white-box twin | Read a no-perm file / pass a wrong PIN |
| 5 | **Hardened target:** build cmu's 2026 design and attack a `MULTI_IF_FAILOUT` site | Know what it takes (or that it's out of reach on a Lite) |
| 6 | **Memory-dump drill:** glitch a length/loop bound to over-read past a buffer | Leaks a planted secret |
| 7 | Tooling: triage grep across packages, an objdump-offset helper, a run logger | One command per team → "soft/hard + candidate checks" |

Drills 2–6 also tell you what to harden in **your own 2027 design**, so the work counts twice.

---

## Sources

- eCTF 2026 rules: [index](https://rules.ectf.mitre.org/2026/index.html) ·
  [attack flags](https://rules.ectf.mitre.org/2026/flags/attack_flags.html) ·
  [security reqs](https://rules.ectf.mitre.org/2026/specs/security_reqs.html) ·
  [system/bootloader](https://rules.ectf.mitre.org/2026/system/bootloader.html) ·
  [MSPM0L2228 getting started](https://rules.ectf.mitre.org/2026/getting_started/mspm0l2228.html) ·
  [rules](https://rules.ectf.mitre.org/rules.html) · [FAQ](https://rules.ectf.mitre.org/faq.html)
- Public 2026 attacks (all software): [Purdue-eCTF/2026-eCTF-attacks](https://github.com/Purdue-eCTF/2026-eCTF-attacks) ·
  2025 write-up: [jacksondonaldson.com/ectf](https://jacksondonaldson.com/ectf)
- TI MSPM0 security (FI countermeasures listed as "Future"; SWD/BSL passwords): [SLAAE29](https://www.ti.com/document-viewer/lit/html/SLAAE29) ·
  [SLAAEO4](https://www.ti.com/document-viewer/lit/html/SLAAEO4/GUID-A043696A-D8C0-4FDB-BD3F-328062F964CC.html)
- MSPM0L2228 datasheet (VCORE 470 nF, BOR, SYSOSC): [SLASF94A](https://ti.com/document-viewer/lit/html/SLASF94A/GUID-818DAD92-48D6-477E-9911-05D409A36286) ·
  TRM SLAU847 (NONMAIN, lockout) [section](https://www.ti.com/document-viewer/lit/html/SLAU847F/GUID-7A8317FD-488A-441D-87AE-D598F942E50D) ·
  NONMAIN brick warnings: [SEGGER KB](https://kb.segger.com/TI_MSPM0L) · [E2E L1228 lockout](https://e2e.ti.com/support/tools/code-composer-studio-group/ccs/f/code-composer-studio-forum/1579657/mspm0l1228-mspm0l1228-locked---dap-error-0x00010136-and-secap_rcr-access-failure)
- MSPM0 BSL user guide: [SLAU887](https://www.ti.com/lit/pdf/slau887)
- ChipWhisperer labs: [Fault 2_2 password bypass](https://rtfm.newae.com/tutorials/CWLITEXMEGA/SOLN_Fault%202_2%20-%20Voltage%20Glitching%20to%20Bypass%20Password) ·
  [Fault 2_3 memory dump](https://rtfm.newae.com/tutorials/CWLITEXMEGA/SOLN_Fault%202_3%20-%20Voltage%20Glitching%20to%20Memory%20Dump) ·
  [forum: tune your own params](https://forum.newae.com/t/the-tutorial-fault-2-2-voltage-glitching-to-bypass-password-isnt-working/5022)
- Background papers: [Shaping the Glitch (TCHES 2019, incl. TI MCUs)](https://tches.iacr.org/index.php/TCHES/article/view/7390) ·
  [Fill your Boots (bootloader FI)](https://tches.iacr.org/index.php/TCHES/article/download/8727/8327/5445) ·
  [Injecting SW vulns with voltage glitching](https://arxiv.org/pdf/1903.08102) ·
  [WOOT'25 Cortex-M33 secure boot FI](https://www.usenix.org/system/files/woot25-muench.pdf)
