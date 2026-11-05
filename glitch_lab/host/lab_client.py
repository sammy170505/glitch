"""
glitch_lab host client — talks to the glitch_lab firmware over the XDS110 backchannel UART.

Two uses:
  1. CLI, no ChipWhisperer needed (run this right after flashing):
        <cwenv python> host/lab_client.py COM7 selftest        # baseline + positive control gate
        <cwenv python> host/lab_client.py COM7 send "1 0"      # one raw command
        <cwenv python> host/lab_client.py COM7 timing2         # L2 side quest: PIN from timing leak
  2. Imported by host/glitch_lab.ipynb, which adds the ChipWhisperer and passes a reset_fn.

Classification (CLAUDE.md "classify richly"):
  CLEAN   normal refusal, cycles match the baseline
  EFFECT  normal refusal but cycles (c=) or a value changed -> fault landed, goal not met
  DETECT  a countermeasure caught the fault (L5/L6)
  GRANT   goal met (wrong PIN accepted / perm bypassed / secret over-read)
  WEIRD   reply present but malformed/unexpected
  NOREPLY no reply (crash, HardFault, reset)  -> caller must reboot + latch-check
"""
import re
import sys
import time

import serial

BAUD = 115200

# What to send for a normal (wrong-input) shot, and for the positive control (correct input).
WRONG = {0: "0", 1: "1 0", 2: "2 000000", 3: "3 1", 4: "4 000000",
         5: "5 000000", 6: "6 000000", 7: "7 0 f", 8: "8 32", 9: "9 0"}
RIGHT = {1: "1 c0ffee", 2: "2 c0ffee", 3: "3 0", 4: "4 c0ffee",
         5: "5 c0ffee", 6: "6 c0ffee", 7: "7 c0ffee f", 9: "9 c0ffee"}  # L0/L8 have no "right" input

_KV = re.compile(r"(\w+)=(\S+)")


def parse_reply(line):
    """'L1 DENY c=7' -> {'level':1,'result':'DENY','c':7,...}; L0 -> {'level':0,'A':200,'c':..}"""
    if not line or not line.startswith("L") or len(line) < 3 or not line[1].isdigit():
        return None
    out = {"level": int(line[1]), "raw": line}
    parts = line.split()
    if len(parts) >= 2 and "=" not in parts[1]:
        out["result"] = parts[1]
    for k, v in _KV.findall(line):
        out[k] = int(v) if v.isdigit() else v
    return out


def classify(level, rep, base_c=None):
    if rep is None:
        return "NOREPLY"
    if rep.get("level") != level:
        return "WEIRD"
    if level == 0:
        if "A" not in rep:
            return "WEIRD"
        return "CLEAN" if rep["A"] == 200 else "EFFECT"
    res = rep.get("result")
    if res == "GRANT":
        return "GRANT"
    if res == "DETECT":
        return "DETECT"
    if res != "DENY":
        return "WEIRD"
    if level == 8 and rep.get("n") != 16:
        return "EFFECT"
    if base_c is not None and rep.get("c") is not None and rep["c"] != base_c:
        return "EFFECT"                      # refusal, but the window took a different path
    return "CLEAN"


class Lab:
    def __init__(self, port, reset_fn=None, baud=BAUD, boot_timeout=2.0):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.reset_fn = reset_fn                   # e.g. CW nRST pulse; None = no hardware reset
        self.boot_timeout = boot_timeout           # insecure bootloader adds boot time; tune it
        self.base_c = {}                           # per-level baseline cycle count

    def close(self):
        self.ser.close()

    def readline(self, timeout=0.3):
        t0 = time.time()
        buf = b""
        while time.time() - t0 < timeout:
            ch = self.ser.read(1)
            if not ch:
                continue
            if ch == b"\n":
                return buf.decode(errors="replace").strip()
            buf += ch
        return buf.decode(errors="replace").strip() or None

    def cmd(self, line, timeout=0.3):
        self.ser.reset_input_buffer()
        self.ser.write((line + "\n").encode())
        return self.readline(timeout)

    def ping(self):
        return self.cmd("p", 0.2) == "PONG"

    def wait_boot(self):
        """Reset (if we can) and wait for the BOOT banner. False => suspect LATCH."""
        self.ser.reset_input_buffer()
        if self.reset_fn:
            self.reset_fn()
        t0 = time.time()
        while time.time() - t0 < self.boot_timeout:
            l = self.readline(0.1)
            if l and l.startswith("BOOT"):
                return True
        return self.ping() if not self.reset_fn else False

    def shot(self, level, arg_line=None, timeout=0.3):
        raw = self.cmd(arg_line or WRONG[level], timeout)
        rep = parse_reply(raw)
        return classify(level, rep, self.base_c.get(level)), rep, raw

    def learn_baseline(self, level, n=20, arg_line=None):
        """Run n clean shots, require identical results, record baseline c. Returns (ok, info).
        arg_line overrides the default wrong input (e.g. '4 c0ffe0'); baseline must use the same input
        you will glitch with, because c= depends on it (L2, L4 variants)."""
        cs, bad = set(), []
        for _ in range(n):
            cls, rep, raw = self.shot(level, arg_line)
            if level == 0:
                ok = cls == "CLEAN"
            else:
                ok = rep is not None and rep.get("result") == "DENY" and (level != 8 or rep.get("n") == 16)
            if not ok:
                bad.append(raw)
            elif rep and "c" in rep:
                cs.add(rep["c"])
        if level != 7 and len(cs) == 1:            # L7 c varies with the random delay by design
            self.base_c[level] = next(iter(cs))
        stable = not bad and (level == 7 or len(cs) == 1)
        return stable, {"c": sorted(cs), "bad": bad[:3]}


def selftest(port, n=20):
    lab = Lab(port)
    print("version:", lab.cmd("v"))
    print("ping   :", "OK" if lab.ping() else "FAIL")
    all_ok = True
    print("\nlevel  baseline(%d wrong)          positive control (right input)" % n)
    for lvl in range(10):
        ok, info = lab.learn_baseline(lvl, n)
        pc = ""
        if lvl in RIGHT:
            cls, rep, raw = lab.shot(lvl, RIGHT[lvl])
            pc_ok = rep is not None and rep.get("result") == "GRANT"
            pc = ("GRANT ok  " if pc_ok else "FAILED -> %r" % raw)
            all_ok &= pc_ok
        all_ok &= ok
        print("L%d     %-6s c=%-14s %s %s" % (lvl, "STABLE" if ok else "UNSTABLE", info["c"],
                                            pc, info["bad"] if info["bad"] else ""))
    print("\nGATE:", "PASS - safe to start glitching" if all_ok else "FAIL - fix before sweeping")
    lab.close()
    return all_ok


def timing2(port, charset="0123456789abcdef", reps=3):
    """L2 side quest: recover the PIN from the c= timing leak alone (no glitch).
    The early-exit compare runs longer for every correct leading char. The LAST char has no
    early exit after it, so it is found by trying each candidate for a GRANT."""
    lab = Lab(port)
    prefix = ""
    for pos in range(5):
        best = None
        for ch in charset:
            guess = (prefix + ch).ljust(6, "0" if ch != "0" else "1")
            cs = [(parse_reply(lab.cmd("2 " + guess)) or {}).get("c", -1) for _ in range(reps)]
            c = min(cs)
            if best is None or c > best[0]:
                best = (c, ch)
        prefix += best[1]
        print("pos %d -> %r   (c=%d)   prefix=%s" % (pos, best[1], best[0], prefix))
    for ch in charset:
        raw = lab.cmd("2 " + prefix + ch)
        if raw and "GRANT" in raw:
            print("PIN =", prefix + ch, "->", raw)
            lab.close()
            return prefix + ch
    print("last char not found; prefix so far:", prefix)
    lab.close()
    return None


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    port, what = sys.argv[1], sys.argv[2]
    if what == "selftest":
        sys.exit(0 if selftest(port, int(sys.argv[3]) if len(sys.argv) > 3 else 20) else 1)
    elif what == "timing2":
        timing2(port)
    elif what == "send":
        lab = Lab(port)
        print(lab.cmd(" ".join(sys.argv[3:]), 0.5))
        lab.close()
    else:
        print(__doc__)
