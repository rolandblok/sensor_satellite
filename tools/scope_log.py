"""Log DC measurements from a Rigol DS1054Z over SCPI.

The handheld meter is more accurate than the scope for slow work, but it cannot
be left alone for three hours.  This turns the scope into an unattended logger
so a charge curve can be captured while the light moves, which is the thing
spot readings keep getting wrong.

    python tools/scope_log.py --list
    python tools/scope_log.py --ip 192.168.1.50 --idn
    python tools/scope_log.py --ip 192.168.1.50 --interval 10 --out vcap.csv

Needs pyvisa and the pure-python backend, no vendor driver over LAN:

    python -m pip install pyvisa pyvisa-py

USB additionally needs `pyusb` and a libusb-bound device; LAN is the easier
route on Windows.  Output is `#`-commented CSV, same shape as the firmware's.
"""
import argparse, csv, os, sys, time

try:
    import pyvisa
except ImportError:
    sys.exit("pyvisa not installed:  python -m pip install pyvisa pyvisa-py")

# VAVG over the whole acquisition.  The scope averages internally, which buys
# back most of what the 8-bit vertical path loses on a slow-moving DC level.
ITEMS = {"vavg": "VAVG", "vrms": "VRMS", "vmax": "VMAX", "vmin": "VMIN",
         "vpp": "VPP"}


def lock(path, force):
    """Refuse to start if another run owns this output file.

    Two loggers on one path interleave their writes at different offsets, and
    the result looks exactly like instrument noise: values that jump around and
    go backwards.  Cheap to prevent, expensive to diagnose.
    """
    lk = path + ".lock"
    try:
        fd = os.open(lk, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        if not force:
            try:
                owner = open(lk).read().strip() or "unknown"
            except OSError:
                owner = "unknown"
            sys.exit(f"{lk} exists (pid {owner}) — another logger is writing "
                     f"{path}. Stop it, or pass --force if the lock is stale.")
        return lk
    os.write(fd, str(os.getpid()).encode())
    os.close(fd)
    return lk


def open_scope(rm, resource):
    dev = rm.open_resource(resource)
    dev.timeout = 5000
    idn = dev.query("*IDN?").strip()
    if "DS1" not in idn.upper() and "RIGOL" not in idn.upper():
        print(f"# warning: unexpected instrument: {idn}", file=sys.stderr)
    return dev, idn


def measure(dev, item, channel):
    """Returns volts, or None when the scope reports no valid measurement."""
    # DS1000Z documents the ITEM form; older Rigol firmware wants VAVerage?.
    for cmd in (f":MEASure:ITEM? {item},CHANnel{channel}",
                f":MEASure:V{item[1:]}erage? CHANnel{channel}"):
        try:
            v = float(dev.query(cmd).strip())
        except Exception:
            continue
        # Rigol returns ~9.9e37 for "measurement unavailable" rather than erroring
        return None if abs(v) > 1e30 else v
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ip", help="scope IP; shorthand for a TCPIP resource")
    ap.add_argument("--resource", help="full VISA resource string")
    ap.add_argument("--list", action="store_true", help="enumerate resources and exit")
    ap.add_argument("--idn", action="store_true", help="identify and exit")
    ap.add_argument("--channel", default="1",
                    help="channel, or comma list ordered high,low e.g. 2,1")
    ap.add_argument("--shunt", type=float, metavar="OHMS",
                    help="adds a current column, (first channel - second) / OHMS. "
                         "Both probe grounds stay on the real ground rail; the "
                         "subtraction is what floats, not the probe.")
    ap.add_argument("--item", default="vavg", choices=sorted(ITEMS))
    ap.add_argument("--interval", type=float, default=10.0, help="seconds between samples")
    ap.add_argument("--duration", type=float, default=0.0, help="seconds; 0 = until Ctrl-C")
    ap.add_argument("--out", help="CSV path; default prints to stdout only")
    ap.add_argument("--force", action="store_true", help="ignore a stale .lock")
    args = ap.parse_args()

    # validate before opening the link: the scope allows one connection at a
    # time, so failing late would lock out whatever is already talking to it
    chans = [int(c) for c in args.channel.split(",") if c.strip()]
    if not chans:
        return ap.error("--channel needs at least one channel")
    if args.shunt and len(chans) != 2:
        return ap.error("--shunt needs exactly two channels, high side first")

    rm = pyvisa.ResourceManager("@py")

    if args.list:
        found = rm.list_resources()
        print("\n".join(found) if found else
              "(nothing found — over LAN, pass --ip; USB needs pyusb + libusb)")
        return 0

    resource = args.resource or (f"TCPIP::{args.ip}::INSTR" if args.ip else None)
    if not resource:
        return ap.error("need --ip or --resource (or --list)")

    dev, idn = open_scope(rm, resource)
    print(f"# {idn}")
    if args.idn:
        return 0

    item = ITEMS[args.item]
    cols = ["t_s"] + [f"ch{c}_V" for c in chans] + (["I_mA"] if args.shunt else [])
    print(f"# {item} on " + ", ".join(f"CHANnel{c}" for c in chans)
          + f", every {args.interval:g} s")
    if args.shunt:
        print(f"# I_mA = (ch{chans[0]} - ch{chans[1]}) / {args.shunt:g} ohm")
    print("# " + ",".join(cols))

    writer = fh = lk = None
    if args.out:
        lk = lock(args.out, args.force)
        fh = open(args.out, "w", newline="")
        writer = csv.writer(fh)
        writer.writerow([f"# {idn}"])
        if args.shunt:
            writer.writerow([f"# I_mA = (ch{chans[0]} - ch{chans[1]}) / {args.shunt:g} ohm"])
        writer.writerow(cols)

    t0 = time.monotonic()
    n = 0
    try:
        while True:
            t = time.monotonic() - t0
            if args.duration and t > args.duration:
                break
            vs = [measure(dev, item, c) for c in chans]
            if any(v is None for v in vs):
                bad = ", ".join(f"CH{c}" for c, v in zip(chans, vs) if v is None)
                print(f"# {t:.1f} no valid measurement on {bad} — check the channel "
                      f"is on and the trace is on screen", file=sys.stderr)
            else:
                row = [f"{t:.1f}"] + [f"{v:.4f}" for v in vs]
                if args.shunt:
                    row.append(f"{(vs[0] - vs[1]) / args.shunt * 1000:.3f}")
                print(",".join(row), flush=True)
                if writer:
                    writer.writerow(row)
                    fh.flush()          # survive a Ctrl-C or a power cut mid-run
                n += 1
            # drift-free cadence: sleep to the next slot, not for a fixed span
            time.sleep(max(0.0, args.interval - ((time.monotonic() - t0) - t)))
    except KeyboardInterrupt:
        print("\n# stopped", file=sys.stderr)
    finally:
        if fh:
            fh.close()
        if lk:
            try:
                os.remove(lk)
            except OSError:
                pass
        dev.close()

    print(f"# {n} samples" + (f" -> {os.path.relpath(args.out)}" if args.out else ""),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
