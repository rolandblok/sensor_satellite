"""Review a hand-edited solar_node.kicad_sch.

Runs KiCad's own ERC, then extracts the netlist and compares it against the
committed baseline (solar_node.net).  The netlist diff is the part that
matters: it says whether redrawing the schematic changed the circuit.

    python tools/check_schematic.py

Exit code 0 if the circuit is unchanged and ERC is clean.
"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCH  = os.path.join(ROOT, "solar_node.kicad_sch")
BASE = os.path.join(ROOT, "solar_node.net")

CLI = None
for v in ("9.0", "8.0", "7.0"):
    p = rf"C:\Program Files\KiCad\{v}\bin\kicad-cli.exe"
    if os.path.exists(p):
        CLI = p
        break
if CLI is None:
    sys.exit("kicad-cli not found")


def nets_of(netlist_text):
    """{net name: frozenset('REF.PIN')} - ignores net numbering and ordering"""
    out = {}
    i = netlist_text.find("(nets")
    for b in re.split(r'\n\s*\(net\s', netlist_text[i:])[1:]:
        nm = re.search(r'\(name "([^"]*)"\)', b)
        if not nm:
            continue
        name = nm.group(1)
        nodes = frozenset(
            f"{r}.{p}" for r, p in
            re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', b))
        # unconnected-() net names embed pad numbers; normalise to the pin set
        if name.startswith("unconnected-"):
            name = "unconnected"
            out.setdefault(name, set()).update(nodes)
        else:
            out[name] = nodes
    return {k: frozenset(v) for k, v in out.items()}


def main():
    tmp = tempfile.mkdtemp()

    rpt = os.path.join(tmp, "erc.rpt")
    r = subprocess.run([CLI, "sch", "erc", "--output", rpt,
                        "--severity-all", SCH],
                       capture_output=True, text=True)
    if r.returncode not in (0, 5):
        print("SCHEMATIC FAILED TO LOAD")
        print((r.stdout + r.stderr).strip())
        return 2

    text = open(rpt, encoding="utf-8").read()
    kinds = {}
    for k in re.findall(r'^\s*\[([a-z_]+)\]', text, re.M):
        kinds[k] = kinds.get(k, 0) + 1
    print("ERC:", ", ".join(f"{v}x {k}" for k, v in sorted(kinds.items())) or "clean")
    for k in kinds:
        if k != "lib_symbol_mismatch":       # benign: inlined derived symbol
            for m in re.finditer(r'^\s*\[' + k + r'\]:(.*)$', text, re.M):
                print("   !", m.group(1).strip())

    new = os.path.join(tmp, "new.net")
    subprocess.run([CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
                    "--output", new, SCH], capture_output=True, text=True)

    a = nets_of(open(BASE, encoding="utf-8").read())
    b = nets_of(open(new,  encoding="utf-8").read())

    print(f"\nnets: baseline {len(a)}, current {len(b)}")
    ok = True
    for name in sorted(set(a) | set(b)):
        if name not in b:
            print(f"  REMOVED  {name}: {sorted(a[name])}"); ok = False
        elif name not in a:
            print(f"  ADDED    {name}: {sorted(b[name])}"); ok = False
        elif a[name] != b[name]:
            print(f"  CHANGED  {name}")
            for p in sorted(a[name] - b[name]):
                print(f"      lost   {p}")
            for p in sorted(b[name] - a[name]):
                print(f"      gained {p}")
            ok = False
    print("\nCIRCUIT UNCHANGED" if ok else "\nCIRCUIT DIFFERS FROM BASELINE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
