"""Read the node's flash log over USB and save it under data/<date>/.

    python tools/dump_log.py              # wait for the XIAO, dump, save
    python tools/dump_log.py --port COM8  # skip the port search

Start this first, then plug USB in (VCAP jumper open) or tap RESET. The script
waits for the C3's native USB port to appear, opens it and keeps sending `d`
until the dump starts - any byte opens the cold-boot command window, so there is
no timing to get right. It needs firmware v1.2 or later on a low cap: earlier
builds sleep at the floor gate before USB comes up, and then only console mode
(tap RESET, then hold BOOT) gets in.

Writes, in data/<YYYY-MM-DD>/ (today, or --date):
    console_<HHMMSS>.txt   everything the board printed, verbatim
    flashlog_<HHMMSS>.csv  the dump itself, the firmware's CSV unchanged

The log is not erased. Do that from the console with `e` once the files are
safe.
"""
import argparse, datetime, os, sys, time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial not installed:  python -m pip install pyserial")

ESPRESSIF_VID = 0x303A   # the C3's on-chip USB-Serial-JTAG
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_port():
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None


def wait_for_port(timeout_s):
    t0 = time.time()
    print("# waiting for the XIAO's USB port - plug in or tap RESET ...")
    while time.time() - t0 < timeout_s:
        port = find_port()
        if port:
            return port
        time.sleep(0.1)
    sys.exit("# no Espressif USB port appeared")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="skip the search, e.g. COM8")
    ap.add_argument("--date", help="folder name under data/, default today")
    ap.add_argument("--wait", type=float, default=300, help="seconds to wait for the port")
    a = ap.parse_args()

    port = a.port or wait_for_port(a.wait)
    print(f"# opening {port}")
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.1
    s.dtr = s.rts = False          # no reset from us; Windows may still do one
    for _ in range(50):            # the node can still be enumerating
        try:
            s.open()
            break
        except serial.SerialException:
            time.sleep(0.1)
    else:
        sys.exit(f"# could not open {port}")

    lines, dump, buf = [], None, b""
    t_poke = 0.0
    t_end = time.time() + 120
    while time.time() < t_end:
        if dump is None and time.time() - t_poke > 0.2:
            try:
                s.write(b"d")
            except serial.SerialException:
                pass
            t_poke = time.time()
        try:
            buf += s.read(4096)
        except serial.SerialException:
            sys.exit("# port went away - the board slept or reset. Try again.")
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            lines.append(line)
            print(line)
            if line.startswith("# flash log dump"):
                dump = []
            elif dump is not None:
                if line.startswith("# end of dump"):
                    t_end = 0
                    break
                if not line.startswith("#"):
                    dump.append(line)
    try:
        s.write(b"c")              # let a cold-boot window carry on rather than wait
    except serial.SerialException:
        pass
    s.close()

    if dump is None:
        sys.exit("# no dump seen")
    out = os.path.join(ROOT, "data", a.date or datetime.date.today().isoformat())
    os.makedirs(out, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%H%M%S")
    with open(os.path.join(out, f"console_{stamp}.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(out, f"flashlog_{stamp}.csv"), "w", encoding="utf-8") as f:
        f.write("\n".join(dump) + "\n")
    print(f"# saved {len(dump) - 1} records to {out}")


if __name__ == "__main__":
    main()
