"""Show or change the node's settings over USB, without reflashing.

    python tools/node_cfg.py                          # show them
    python tools/node_cfg.py cycle_s=180 poll_s=60    # change, then show
    python tools/node_cfg.py --defaults               # back to the #defines
    python tools/node_cfg.py --erase                  # erase the flash log

Names: cycle_s (sleep between updates), poll_s (sleep between held-off polls),
v_hold, v_resume, v_floor (volts). The node refuses anything that would strand
it - v_floor < v_hold < v_resume, all within 2.50-5.00 V, cycle_s >= 60,
poll_s >= 10 - and says why.

Start this first, then plug USB in (VCAP jumper open) or tap RESET. Needs
firmware v1.3 or later. Settings live in the NVS partition, so they survive
power loss and reflashing; the flash log is not touched.
"""
import argparse, sys, time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial not installed:  python -m pip install pyserial")

NAMES = ("cycle_s", "poll_s", "v_hold", "v_resume", "v_floor")


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x303A:      # the C3's on-chip USB-Serial-JTAG
            return p.device
    return None


_buf = b""   # bytes read past the last line asked for - kept for the next call


def read_until(s, marks, timeout):
    """Print lines until one starts with any of marks; returns that line."""
    global _buf
    t_end = time.time() + timeout
    while time.time() < t_end:
        _buf += s.read(4096)
        while b"\n" in _buf:
            raw, _buf = _buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            print(line)
            if any(line.startswith(m) for m in marks):
                return line
    return None


def main():
    global _buf
    ap = argparse.ArgumentParser()
    ap.add_argument("set", nargs="*", help="name=value pairs")
    ap.add_argument("--defaults", action="store_true", help="restore the #define defaults")
    ap.add_argument("--erase", action="store_true",
                    help="erase the flash log - dump it first with tools/dump_log.py")
    ap.add_argument("--port")
    ap.add_argument("--wait", type=float, default=300)
    a = ap.parse_args()

    pairs = []
    for kv in a.set:
        name, _, val = kv.partition("=")
        if name not in NAMES or not val:
            sys.exit(f"bad setting '{kv}' - use name=value, names: {' '.join(NAMES)}")
        float(val)               # fail here, not on the node
        pairs.append((name, val))

    port = a.port
    if not port:
        print("# waiting for the XIAO's USB port - plug in or tap RESET ...")
        t0 = time.time()
        while not (port := find_port()):
            if time.time() - t0 > a.wait:
                sys.exit("# no Espressif USB port appeared")
            time.sleep(0.1)
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.1
    s.dtr = s.rts = False
    for _ in range(50):
        try:
            s.open()
            break
        except serial.SerialException:
            time.sleep(0.1)
    else:
        sys.exit(f"# could not open {port}")

    # Any byte opens the cold-boot command window; keep poking until it answers.
    t_end = time.time() + 30
    shown = None
    while time.time() < t_end and not shown:
        s.write(b"p")
        shown = read_until(s, ["# settings (flash)"], 0.3)
    if not shown:
        sys.exit("# no answer - is it v1.3 or later? Or try console mode: tap RESET, then hold BOOT")
    read_until(s, ["# defaults"], 2)
    time.sleep(0.3)
    s.reset_input_buffer()       # extra 'p's from the poking
    _buf = b""

    if a.defaults:
        s.write(b"r")
        read_until(s, ["# defaults:"], 5)
    if a.erase:
        s.write(b"e")            # 1.375 MB of sector erases: several seconds
        read_until(s, ["# flash log erased", "# ERASE FAILED"], 60)
    for name, val in pairs:
        s.write(f"s {name} {val}\n".encode())
        if read_until(s, ["# defaults:", "# set:"], 5) is None:
            print(f"# no reply to {name}")
    s.write(b"c")                # carry on: one cycle, then sleep
    s.close()


if __name__ == "__main__":
    main()
