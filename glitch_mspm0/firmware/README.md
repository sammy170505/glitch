# MSPM0L2228 glitch target — firmware

Astra's recommended *first* target: simple LDO core rail, known to glitch. This is her
unrolled `a += 1` ×200 detector (`add_block.h`) wrapped in a UART-gated, GPIO-triggered
protocol so the ChipWhisperer campaign stays in sync and every reset is latch-checked.

## Files
- `add_block.h` — the 200× unrolled volatile increment (the vulnerable region).
- `main.c` — reset→`BOOT`, then per command: `g` run+report, `t` detection self-test, `v` id.

## Hardware prep (the important part)

VCORE on this board is **C3 (0.47 µF, 0402) straight to GND, no series R** — confirmed from
the schematic (C3 pin1 = U1 pin 32 VCORE). That is the crowbar injection node.

**Astra's best-working mod — do this first:**
1. Remove **C3**.
2. Replace it with **≈480 nF in series with ≈36 Ω**, across the VCORE pad → GND
   (i.e. VCORE → 480 nF → 36 Ω → GND). The 36 Ω damps the node so the crowbar makes a
   sharper, controlled dip. Fiddly to solder at 0402 — a small hand-built R+C works.
3. Inject the crowbar at the **VCORE side** of that network (the old C3 VCORE pad).

Fallbacks she gave: a single *somewhat smaller* cap in place of C3 also works. **Do not run
with no cap** — the chip tends to shut down during boot.

> Expected working widths (her data, scaled to this board's 32 MHz): ~1–3 target cycles
> ≈ 31–94 ns. Keep pulses under ~20 cycles (~625 ns) to avoid frying the part. The notebook
> converts this to CW `repeat` counts for you.

## How we connect to the laptop (eCTF getting-started)

Per MITRE's MSPM0L2228 getting-started page: the board is connected with an **XDS110
debugger** via **8 female-to-female jumper wires** to the board's 8-pin debug header, matching
silkscreen labels (`3V3, GND, SWDIO, SWCLK, RXD=PA11, TXD=PA10, NRST, BSL`). The XDS110's USB
end plugs into the laptop and shows up as a **serial device**. There is **no onboard USB** on
the LITO board, and **no separate USB-UART dongle is needed** — the host talks to the target
over the **XDS110 backchannel UART**, which lands on **PA10/PA11 = UART0** (same 8-wire hookup).

- **Flashing:** **OpenOCD** over the XDS110 SWD (configs below).
- **Host serial:** the XDS110 backchannel COM port, **115200 8-N-1** (confirmed from the
  reference design's `ti_msp_dl_config.c`: UART0 and UART1 both 115200, clock = BUSCLK/32 MHz).
  Re-check the port each session: `[System.IO.Ports.SerialPort]::GetPortNames()`.
- **Board-to-board / challenge comms:** the eCTF design uses **UART1 = PA8 (TX) / PA9 (RX)**,
  *not* the host link. Only relevant when you move on to glitching the real challenge firmware.

## Wiring for the glitch campaign (CW-Lite + XDS110 together)
| Signal        | From → To                                  | Notes |
|---------------|--------------------------------------------|-------|
| trigger       | board PB0 → CW `tio4`                       | `scope.trigger.triggers='tio4'` |
| crowbar LP    | VCORE node (old C3 pad) → CW LP glitch      | **LP FET only, never HP** |
| GND           | board GND ↔ CW GND ↔ XDS110 GND             | common ground for all three |
| host UART     | board PA10/PA11 ↔ XDS110 RXD/TXD (+3V3)     | backchannel serial, keep connected |
| reset         | board NRST → **CW `nRST`**                  | see conflict note below |

**nRST ownership conflict (important):** both the XDS110 and the CW want to drive NRST. For the
glitch loop, let the **CW own NRST**: flash with all 8 XDS110 wires connected, then **remove the
XDS110 → NRST (and SWDIO/SWCLK) jumpers**, keeping only XDS110 `3V3, GND, RXD, TXD` for the
backchannel serial, and wire **CW `nRST` → board NRST**. The backchannel UART works without the
SWD pins. (Alternative: leave SWD attached and reset via OpenOCD between shots — slower, and
risks contention on the NRST net.)

## Build (TI MSPM0 SDK + SYSCONFIG)
Start from the SDK's `empty` example for `LP_MSPM0L2228` (or an `MSPM0L2228` custom board) so
you get a working linker script + startup + `SYSCFG_DL_init()`, then:

1. Drop `main.c` and `add_block.h` into the project.
2. In SysConfig (`.syscfg`) add/configure:
   - **SYSCTL**: MCLK = 32 MHz (SYSOSC).
   - **GPIO** "TRIG": PB0, output, start low. Make the generated name resolve to
     `TRIG_PORT`/`TRIG_PIN` (or just edit the two `#define`s at the top of `main.c`).
   - **GPIO** "LED": PB14, output (optional heartbeat).
   - **UART (Main)** instance on **UART0**, PA10=TX / PA11=RX, 115200-8-N-1, FIFOs on.
     Resolve its base to `GLITCH_UART` (default `UART0`).
3. Build with the SDK (CCS or `gcc` arm-none-eabi via the SDK makefiles) → produces an `.elf`
   (or `.out`). Flash it with OpenOCD (below).

If your SYSCONFIG uses different symbol names, the only edits needed are the five `#define`s
at the top of `main.c`.

## Flash & debug (OpenOCD + XDS110)

MSPM0L2228 support needs the **latest pre-release OpenOCD**
(github.com/openocd-org/openocd/releases/tag/latest); the eCTF Docker image already has it.

**eCTF's init command (verbatim)** — starts OpenOCD and halts the target, leaving GDB (:3333)
and telnet (:4444) servers up:
```
openocd -s ./share/openocd/scripts/ -f interface/xds110.cfg -f target/ti/mspm0.cfg -c 'bindto 0.0.0.0; init; halt'
```
Debug from GDB (eCTF-documented):
```
gdb-multiarch <your_firmware.elf>
(gdb) target remote localhost:3333
```
…or a telnet console: `telnet localhost 4444`.

**Flash in one shot, then exit** (standard OpenOCD `program`, using eCTF's configs) — the form
to use here, so OpenOCD releases SWD/NRST and you can hand NRST to the ChipWhisperer:
```
openocd -s ./share/openocd/scripts/ -f interface/xds110.cfg -f target/ti/mspm0.cfg \
        -c 'init; reset halt; program <your_firmware.elf> verify; reset run; exit'
```
(Or from the telnet console: `reset halt` then `program <your_firmware.elf> verify reset`.)
Replace `<your_firmware.elf>` with your build output. The eCTF challenge ELF is `hsm.elf`; your
glitch-test build will have its own name.

## Sanity check before glitching
`v` → `MSPM0L2228-GLITCH v1`, `g` → `A=200` every time (baseline), `t` → `A=199`
(proves the Glitched path). Then run the notebook's baseline gate + positive control.
