#!/usr/bin/env python3
"""Talk to the AICW Stick console over USB serial (macOS / Linux / Windows).

  stick.py status                 -> @aicw_status
  stick.py wifi                   -> asks SSID + password (hidden), sends >wifi=, then status
  stick.py host relay:<hostId>    -> >aicw_host=
  stick.py log [seconds]          -> raw boot / runtime log
"""
import getpass, glob, sys, time
import serial

def port():
    p = sorted(glob.glob("/dev/cu.usbmodem*")) or ["COM6"]
    return p[0]

def talk(cmds, wait=6, want=None):
    s = serial.Serial(port(), 115200, timeout=0.3)
    out = b""
    for c in cmds:
        s.write(b"\n" + c.encode() + b"\n")
        time.sleep(0.4)
    t = time.time()
    while time.time() - t < wait:
        out += s.read(4096)
        if want and all(w.encode() in out for w in want):
            break
    s.close()
    return out.decode("utf8", "replace")

def lines(txt, keys):
    return [l for l in txt.splitlines() if any(k in l for k in keys)]

a = sys.argv[1:] or ["status"]
if a[0] == "status":
    print("\n".join(lines(talk([">aicw_status"], 6, ["@aicw_status"]), ["@aicw"])) or "NO REPLY")
elif a[0] == "wifi":
    ssid = input("Ten Wi-Fi (SSID): ").strip()
    pw = getpass.getpass("Mat khau (an): ")
    r = talk([f">wifi={ssid}\t{pw}"], 20, ["@wifi"])
    print("\n".join(lines(r, ["@wifi"])) or "KHONG THAY @wifi")
    time.sleep(8)
    print("\n".join(lines(talk([">aicw_status"], 8, ["@aicw_status"]), ["@aicw"])) or "KHONG THAY @aicw_status")
elif a[0] == "host":
    print("\n".join(lines(talk([">aicw_host=" + a[1]], 6, ["@aicw_host"]), ["@aicw"])) or "NO REPLY")
elif a[0] == "log":
    secs = float(a[1]) if len(a) > 1 else 20
    s = serial.Serial(port(), 115200, timeout=0.3)
    t = time.time()
    while time.time() - t < secs:
        b = s.read(4096)
        if b:
            sys.stdout.write(b.decode("utf8", "replace")); sys.stdout.flush()
