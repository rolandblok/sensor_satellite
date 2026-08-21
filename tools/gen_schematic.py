"""Generate solar_node.kicad_sch for the sensor_satellite project.

Symbol definitions are lifted verbatim from the installed KiCad 9 libraries and
pin coordinates are read from them, so every wire endpoint is computed rather
than guessed.  Nets are made with labels on short stubs - a netlist-style
schematic that passes ERC without hand-routing.

REQUIRES KICAD, AND KICAD IS NO LONGER INSTALLED HERE.  The KiCad project was
removed from the repo on 2026-08-21; this script will fail at SYMDIR below until
KiCad 9 is reinstalled.  It is kept because the component and net declarations
near the bottom are the circuit definition of record - the most precise
description of the design in this repo, readable whether or not it can run.
solar_node.net is what it last produced, and solar_node.pdf/.svg render it.

Out of date as of 2026-08-21: design note 9 adds the USB feed D3 with its 22 ohm
inrush limiter, and the regulator is now an HT7533 rather than the MCP1700 here.
Neither change is reflected below.
"""
import os, re, math, uuid as _uuid

SYMDIR  = r"C:\Program Files\KiCad\9.0\share\kicad\symbols"
OUT     = r"C:\githubclones\sensor_satellite\solar_node.kicad_sch"
PROJECT = "solar_node"

# ---------------------------------------------------------------- lib parsing
def _block(s, i):
    d = 0
    for j in range(i, len(s)):
        if s[j] == '(':
            d += 1
        elif s[j] == ')':
            d -= 1
            if d == 0:
                return s[i:j + 1]
    raise ValueError("unbalanced")

_libcache = {}
def lib_text(lib):
    if lib not in _libcache:
        _libcache[lib] = open(os.path.join(SYMDIR, lib + ".kicad_sym"),
                              encoding="utf-8").read()
    return _libcache[lib]

def symbol_def(lib, name):
    txt = lib_text(lib)
    m = re.search(r'^\t\(symbol "' + re.escape(name) + r'"', txt, re.M)
    if not m:
        raise KeyError(f"{lib}:{name} not found")
    return _block(txt, m.start() + 1)

def parent_of(defn):
    """derived symbols carry (extends "BASE") and define no pins themselves"""
    m = re.search(r'\(extends "([^"]+)"', defn)
    return m.group(1) if m else None

def flatten(defn, name, pdefn, pname):
    """Inline a derived symbol's parent graphic units, removing (extends ...)."""
    defn = re.sub(r'\n?\s*\(extends "[^"]+"\)', '', defn, count=1)
    subs = []
    for m in re.finditer(r'\(symbol "' + re.escape(pname) + r'_\d+_\d+"', pdefn):
        b = _block(pdefn, m.start())
        b = b.replace(f'(symbol "{pname}_', f'(symbol "{name}_', 1)
        subs.append(b)
    if not subs:
        raise ValueError(f"no graphic units found in parent {pname}")
    inner = "\n".join("\t" + s.replace("\n", "\n\t") for s in subs)
    cut = defn.rfind(')')
    return defn[:cut].rstrip() + "\n" + inner + "\n)"

PIN_RE = re.compile(
    r'\(pin\s+\w+\s+\w+\s*\n?\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)'
    r'.*?\(name\s+"([^"]*)".*?\(number\s+"([^"]+)"', re.S)

def symbol_pins(defn):
    """{number: (x, y, angle, name)} in symbol coordinates."""
    out = {}
    for x, y, a, nm, num in PIN_RE.findall(defn):
        out[num] = (float(x), float(y), float(a), nm)
    return out

# ---------------------------------------------------------------- schematic
class Sch:
    def __init__(self, title):
        self.uuid = str(_uuid.uuid4())
        self.title = title
        self.libs = {}          # "Device:R" -> definition text
        self.pins = {}          # "Device:R" -> {num: (x,y,ang)}
        self.items = []         # rendered s-expressions
        self.instances = []     # (ref, unit, uuid)

    def use(self, lib, name):
        key = f"{lib}:{name}"
        if key not in self.libs:
            d = symbol_def(lib, name)
            par = parent_of(d)
            if par:
                # Derived symbol: its geometry and pins live in the base symbol.
                # A schematic's lib_symbols cannot resolve (extends ...) across
                # the lib-qualified names used here, so inline the parent's
                # graphic units instead and drop the extends.
                d = flatten(d, name, symbol_def(lib, par), par)
            self.pins[key] = symbol_pins(d)
            d = re.sub(r'^\(symbol "' + re.escape(name) + r'"',
                       f'(symbol "{key}"', d, count=1)
            self.libs[key] = d
        return key

    def pin_named(self, inst, want):
        key = inst[0]
        for num, (_, _, _, nm) in self.pins[key].items():
            if nm.upper() == want.upper():
                return num
        raise KeyError(f"{key} has no pin named {want}: "
                       f"{[(n, p[3]) for n, p in self.pins[key].items()]}")

    def place(self, lib, name, ref, value, x, y, footprint="", rot=0):
        # snap to the 2.54 mm grid so every pin endpoint lands on the 1.27 mm
        # connection grid - ERC flags anything off it
        x = round(round(x / 2.54) * 2.54, 2)
        y = round(round(y / 2.54) * 2.54, 2)
        key = self.use(lib, name)
        u = str(_uuid.uuid4())
        pins = self.pins[key]
        pin_sexp = "".join(
            f'\n\t\t(pin "{n}"\n\t\t\t(uuid "{_uuid.uuid4()}")\n\t\t)'
            for n in sorted(pins))
        self.items.append(f'''\t(symbol
\t\t(lib_id "{key}")
\t\t(at {x} {y} {rot})
\t\t(unit 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(dnp no)
\t\t(uuid "{u}")
\t\t(property "Reference" "{ref}"
\t\t\t(at {x + 5.08} {y - 2.54} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left)
\t\t\t)
\t\t)
\t\t(property "Value" "{value}"
\t\t\t(at {x + 5.08} {y} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left)
\t\t\t)
\t\t)
\t\t(property "Footprint" "{footprint}"
\t\t\t(at {x} {y} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Datasheet" "~"
\t\t\t(at {x} {y} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Description" ""
\t\t\t(at {x} {y} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t){pin_sexp}
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "/{self.uuid}"
\t\t\t\t\t(reference "{ref}")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)''')
        return (key, x, y, rot)

    def pin_at(self, inst, num):
        """schematic coordinates of a pin's connection point"""
        key, x, y, rot = inst
        px, py, _, _ = self.pins[key][num]
        if rot:
            a = math.radians(rot)
            px, py = px * math.cos(a) - py * math.sin(a), \
                     px * math.sin(a) + py * math.cos(a)
        return (round(x + px, 4), round(y - py, 4))

    def pin_dir(self, inst, num):
        """unit vector pointing away from the symbol body, schematic coords"""
        key, x, y, rot = inst
        _, _, ang, _ = self.pins[key][num]
        free = math.radians(ang + 180 + rot)
        dx, dy = math.cos(free), -math.sin(free)
        return (round(dx), round(dy))

    def wire(self, x1, y1, x2, y2):
        self.items.append(f'''\t(wire
\t\t(pts
\t\t\t(xy {x1} {y1}) (xy {x2} {y2})
\t\t)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(uuid "{_uuid.uuid4()}")
\t)''')

    def label(self, name, x, y, rot=0, justify="left"):
        self.items.append(f'''\t(label "{name}"
\t\t(at {x} {y} {rot})
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify {justify} bottom)
\t\t)
\t\t(uuid "{_uuid.uuid4()}")
\t)''')

    def net(self, inst, num, name, stub=5.08):
        """stub wire out of a pin, with a net label at its end"""
        x, y = self.pin_at(inst, num)
        dx, dy = self.pin_dir(inst, num)
        ex, ey = round(x + dx * stub, 4), round(y + dy * stub, 4)
        self.wire(x, y, ex, ey)
        rot = 0 if dx >= 0 else 180
        if dx == 0:
            rot = 90 if dy < 0 else 270
        self.label(name, ex, ey, rot)

    def nc(self, inst, num):
        """no-connect flag on a deliberately unused pin"""
        x, y = self.pin_at(inst, num)
        self.items.append(f'''\t(no_connect
\t\t(at {x} {y})
\t\t(uuid "{_uuid.uuid4()}")
\t)''')

    def power(self, lib, name, ref, x, y, rot=0):
        return self.place(lib, name, ref, name, x, y, rot=rot)

    def text(self, s, x, y, size=2.0):
        self.items.append(f'''\t(text "{s}"
\t\t(exclude_from_sim no)
\t\t(at {x} {y} 0)
\t\t(effects
\t\t\t(font
\t\t\t\t(size {size} {size})
\t\t\t)
\t\t\t(justify left bottom)
\t\t)
\t\t(uuid "{_uuid.uuid4()}")
\t)''')

    def render(self):
        libs = "\n".join(
            "\t\t" + self.libs[k].replace("\n", "\n\t\t")
            for k in sorted(self.libs))
        body = "\n".join(self.items)
        return f'''(kicad_sch
\t(version 20250114)
\t(generator "eeschema")
\t(generator_version "9.0")
\t(uuid "{self.uuid}")
\t(paper "A3")
\t(title_block
\t\t(title "{self.title}")
\t\t(rev "A")
\t\t(company "sensor satellite")
\t)
\t(lib_symbols
{libs}
\t)
{body}
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "1")
\t\t)
\t)
\t(embedded_fonts no)
)
'''

# ---------------------------------------------------------------- the circuit
s = Sch("sensor satellite - solar / supercap power chain")

s.text("HARVEST  -  2 x 5V panel in parallel, one blocking diode each", 20, 20)
s.text("STORE  -  4F supercap as 2 x 8F cells, balanced, with 5.1V clamp", 110, 20)
s.text("LOAD  -  low-Iq LDO, ESP32-C3, BME280, 2.9in e-paper", 230, 20)

# --- harvest ---------------------------------------------------------------
sp1 = s.place("Device", "Solar_Cell", "SP1", "5V 200mA", 30, 45)
sp2 = s.place("Device", "Solar_Cell", "SP2", "5V 200mA", 30, 75)
d1  = s.place("Device", "D_Schottky", "D1", "1N5819", 55, 40)
d2  = s.place("Device", "D_Schottky", "D2", "1N5819", 55, 70)

# Solar_Cell pin 1 = "+", pin 2 = "-"
s.net(sp1, s.pin_named(sp1, "+"), "SOL1")
s.net(sp1, s.pin_named(sp1, "-"), "GND")
s.net(sp2, s.pin_named(sp2, "+"), "SOL2")
s.net(sp2, s.pin_named(sp2, "-"), "GND")

# Blocking diodes: anode to the panel, cathode to the cap, so current can only
# flow panel -> cap.  D_Schottky pin 1 is K and pin 2 is A - getting this
# backwards would block charging entirely.
s.net(d1, s.pin_named(d1, "A"), "SOL1")
s.net(d1, s.pin_named(d1, "K"), "VCAP")
s.net(d2, s.pin_named(d2, "A"), "SOL2")
s.net(d2, s.pin_named(d2, "K"), "VCAP")

# --- store & protect -------------------------------------------------------
dz  = s.place("Device", "D_Zener",     "D3", "5.1V clamp", 115, 40)
c1  = s.place("Device", "C_Polarized", "C1", "8F 2.7V",    140, 40)
c2  = s.place("Device", "C_Polarized", "C2", "8F 2.7V",    140, 70)
r1  = s.place("Device", "R",           "R1", "100k bal",   165, 40)
r2  = s.place("Device", "R",           "R2", "100k bal",   165, 70)
r3  = s.place("Device", "R",           "R3", "1M",         190, 40)
r4  = s.place("Device", "R",           "R4", "1M",         190, 70)
c3  = s.place("Device", "C",           "C3", "100n",       190, 100)

# Zener shunt clamp: cathode to VCAP, anode to GND, so it conducts only above
# its breakdown voltage.
s.net(dz, s.pin_named(dz, "K"), "VCAP")
s.net(dz, s.pin_named(dz, "A"), "GND")
s.net(c1, "1", "VCAP");  s.net(c1, "2", "VMID")
s.net(c2, "1", "VMID");  s.net(c2, "2", "GND")
s.net(r1, "1", "VCAP");  s.net(r1, "2", "VMID")
s.net(r2, "1", "VMID");  s.net(r2, "2", "GND")
s.net(r3, "1", "VCAP");  s.net(r3, "2", "VSENSE")
s.net(r4, "1", "VSENSE");s.net(r4, "2", "GND")
s.net(c3, "1", "VSENSE");s.net(c3, "2", "GND")

# --- regulate --------------------------------------------------------------
u1 = s.place("Regulator_Linear", "MCP1700x-330xxTO", "U1", "MCP1700-3302 3V3", 230, 45)
c4 = s.place("Device", "C", "C4", "1u", 215, 75)
c5 = s.place("Device", "C", "C5", "1u", 250, 75)

print("U1 pins:", [(n, p[3]) for n, p in sorted(s.pins[u1[0]].items())])
s.net(u1, s.pin_named(u1, "GND"), "GND")
s.net(u1, s.pin_named(u1, "VI"),  "VCAP")
s.net(u1, s.pin_named(u1, "VO"),  "+3V3")
s.net(c4, "1", "VCAP");  s.net(c4, "2", "GND")
s.net(c5, "1", "+3V3");  s.net(c5, "2", "GND")

# --- load ------------------------------------------------------------------
j1 = s.place("Connector_Generic", "Conn_01x08", "J1", "ESP32-C3 left",  290, 40)
j2 = s.place("Connector_Generic", "Conn_01x08", "J2", "ESP32-C3 right", 290, 90)
j3 = s.place("Connector_Generic", "Conn_01x08", "J3", "2.9in e-paper",  345, 40)
j4 = s.place("Connector_Generic", "Conn_01x06", "J4", "BME280",         345, 90)

# J1/J2 stand in for the SuperMini's two pin rows.  Physical pin ORDER on the
# board has not been verified - check against the module before laying out a
# PCB.  The net names are what matter here.
#
# The 5 V pin is deliberately left unconnected: the external LDO feeds 3V3
# directly, bypassing the SuperMini's onboard regulator, which is the whole
# point of using a low-Iq part.
esp_left  = ["+3V3", "GND", None, "VSENSE", "SDA", "SCL", "EPD_DC", "EPD_CLK"]
esp_right = ["EPD_RST", "EPD_DIN", "EPD_CS", "EPD_BUSY", None, None, None, None]
for i, n in enumerate(esp_left, 1):
    s.net(j1, str(i), n) if n else s.nc(j1, str(i))
for i, n in enumerate(esp_right, 1):
    s.net(j2, str(i), n) if n else s.nc(j2, str(i))

# e-paper: VCC GND DIN CLK CS DC RST BUSY
for i, n in enumerate(["+3V3", "GND", "EPD_DIN", "EPD_CLK", "EPD_CS",
                       "EPD_DC", "EPD_RST", "EPD_BUSY"], 1):
    s.net(j3, str(i), n)

# BME280: VCC GND SCL SDA CSB SDO
# CSB high selects I2C, SDO low selects address 0x76 - see proto_epaper_esp32c3.md
for i, n in enumerate(["+3V3", "GND", "SCL", "SDA", "+3V3", "GND"], 1):
    s.net(j4, str(i), n)

# --- power flags -----------------------------------------------------------
# VCAP and GND are driven only by passive pins, so ERC needs telling they are
# real supplies.  +3V3 gets no flag: the regulator's VO already drives it, and
# a second power output on the same net is an ERC error.
pf1 = s.place("power", "PWR_FLAG", "#FLG01", "PWR_FLAG", 90, 115)
pf2 = s.place("power", "PWR_FLAG", "#FLG02", "PWR_FLAG", 115, 115)
s.net(pf1, "1", "VCAP")
s.net(pf2, "1", "GND")

open(OUT, "w", encoding="utf-8").write(s.render())
print("wrote", OUT)
print("symbols:", len(s.libs), " items:", len(s.items))

# minimal project file so the schematic opens directly from the file manager
import json
pro = {
    "board": {"design_settings": {}, "layer_presets": [], "viewports": []},
    "boards": [],
    "cvpcb": {"equivalence_files": []},
    "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
    "meta": {"filename": PROJECT + ".kicad_pro", "version": 3},
    "net_settings": {
        "classes": [{
            "bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25,
            "diff_pair_width": 0.2, "line_style": 0, "microvia_diameter": 0.3,
            "microvia_drill": 0.1, "name": "Default", "pcb_color": "rgba(0, 0, 0, 0.000)",
            "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": 0.2,
            "via_diameter": 0.6, "via_drill": 0.3, "wire_width": 6
        }],
        "meta": {"version": 3}
    },
    "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
    "schematic": {
        "annotate_start_num": 0,
        "drawing": {"default_line_thickness": 6.0, "default_text_size": 50.0},
        "legacy_lib_dir": "", "legacy_lib_list": [],
        "meta": {"version": 1},
        "page_layout_descr_file": "",
        "spice_current_sheet_as_root": False,
    },
    "sheets": [[s.uuid, "Root"]],
    "text_variables": {},
}
pro_path = os.path.join(os.path.dirname(OUT), PROJECT + ".kicad_pro")
open(pro_path, "w", encoding="utf-8").write(json.dumps(pro, indent=2))
print("wrote", pro_path)
