"""Wiring diagram for one walkie talkie unit (Adafruit ESP32 Feather V2).

Run:  python3 docs/draw_wiring.py   ->  docs/wiring.png and docs/wiring.svg
The Feather is drawn USB-up. Header order follows Adafruit's standard Feather layout;
always wire by the silkscreen label printed next to each pin.
"""
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Circle, Rectangle

plt.rcParams["font.family"] = "DejaVu Sans"

C = {
    "3V": "#d62728", "GND": "#111111", "BAT": "#ff7f0e",
    "MIC_SCK": "#1f77b4", "MIC_WS": "#17becf", "MIC_SD": "#2ca02c",
    "AMP_BCLK": "#9467bd", "AMP_LRC": "#e377c2", "AMP_DIN": "#8c564b", "AMP_SD": "#bcbd22",
    "SCL": "#1f77b4", "SDA": "#2ca02c",
    "ENC_A": "#9467bd", "ENC_B": "#e377c2", "ENC_SW": "#17becf", "PTT": "#ff7f0e",
    "EN": "#7f7f7f", "TP": "#7f7f7f",
}

fig, ax = plt.subplots(figsize=(15, 11), dpi=150)
ax.set_xlim(-2, 116)
ax.set_ylim(-14, 93)
ax.set_aspect("equal")
ax.axis("off")

# ---------------------------------------------------------------- Feather
FX0, FX1 = 44, 64          # left and right header columns
TOP = 66                   # y of first left-header pin
STEP = 3.0
left_pins = ["RST", "3V", "NC", "GND", "A0 / 26", "A1 / 25", "A2 / 34", "A3 / 39", "A4 / 36",
             "A5 / 4", "SCK / 5", "MO / 19", "MI / 21", "RX / 7", "TX / 8", "37"]
right_pins = ["BAT", "EN", "USB", "13", "12", "27", "33", "15", "32", "14", "SCL / 20", "SDA / 22"]
LY = {name.split(" ")[0]: TOP - i * STEP for i, name in enumerate(left_pins)}
RY = {name.split(" ")[0]: TOP - (i + 2) * STEP for i, name in enumerate(right_pins)}

ax.add_patch(FancyBboxPatch((FX0 - 1.2, TOP - 15 * STEP - 3.5), FX1 - FX0 + 2.4, 15 * STEP + 9,
                            boxstyle="round,pad=0,rounding_size=2", fc="#2f3b52", ec="#1b2333", lw=1.5))
ax.add_patch(Rectangle((51, TOP + 3.2), 6, 3, fc="#c0c0c0", ec="#555"))       # USB-C
ax.text(54, TOP + 7.2, "USB-C", ha="center", fontsize=8)
ax.add_patch(Rectangle((FX1 - 4.5, TOP - 1.0), 3, 2.6, fc="#f2f2f2", ec="#555"))  # JST
ax.text(FX1 - 3.0, TOP + 2.3, "JST", ha="center", fontsize=6.5, color="white")
ax.text(54, TOP - 22, "Adafruit ESP32 Feather V2", ha="center", va="center", color="white",
        fontsize=10, fontweight="bold", rotation=90)

used = {"3V", "GND", "A0", "A1", "A2", "A5", "SCK", "MO", "MI", "RX", "TX",
        "BAT", "EN", "13", "27", "33", "15", "32", "14", "SCL", "SDA"}
for name in left_pins:
    key = name.split(" ")[0]
    y = LY[key]
    ax.add_patch(Rectangle((FX0 - 0.6, y - 0.6), 1.2, 1.2, fc="#e8c547" if key in used else "#888", ec="#444"))
    ax.text(FX0 + 1.3, y, name, va="center", fontsize=7, color="white" if key in used else "#9aa6bb")
for name in right_pins:
    key = name.split(" ")[0]
    y = RY[key]
    ax.add_patch(Rectangle((FX1 - 0.6, y - 0.6), 1.2, 1.2, fc="#e8c547" if key in used else "#888", ec="#444"))
    ax.text(FX1 - 1.3, y, name, va="center", ha="right", fontsize=7, color="white" if key in used else "#9aa6bb")


def wire(points, color, lw=2.2, dot_start=False, dot_end=False):
    xs, ys = zip(*points)
    ax.plot(xs, ys, color=color, lw=lw, solid_capstyle="round", zorder=3)
    for flag, p in ((dot_start, points[0]), (dot_end, points[-1])):
        if flag:
            ax.add_patch(Circle(p, 0.55, color=color, zorder=4))


def module(x, y, w, h, title, sub=None, title_below=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=1", fc="#1e5f3b",
                                ec="#0f3320", lw=1.5, zorder=2))
    if title_below:
        ax.text(x + w / 2, y - 1.0, title, ha="center", va="top", fontsize=10, fontweight="bold")
        if sub:
            ax.text(x + w / 2, y - 4.2, sub, ha="center", va="top", fontsize=7.5, color="#444")
        return
    ax.text(x + w / 2, y + h + 1.2, title, ha="center", va="bottom", fontsize=10, fontweight="bold")
    if sub:
        ax.text(x + w / 2, y - 1.0, sub, ha="center", va="top", fontsize=7.5, color="#444")


def mpin(x, y, label, side):
    ax.add_patch(Circle((x, y), 0.5, fc="#e8c547", ec="#444", zorder=5))
    if side == "right":   # pin on the module's right edge, label inside to the left
        ax.text(x - 1.0, y, label, ha="right", va="center", fontsize=7, color="white", zorder=5)
    else:
        ax.text(x + 1.0, y, label, ha="left", va="center", fontsize=7, color="white", zorder=5)


# ---------------------------------------------------------------- power buses
GND_L, V3_L = 36, 39       # left side bus x
GND_R, V3_R = 72, 75       # right side bus x
BUS_TOP = TOP + 11
BUS_BOT = -6
# Left buses
wire([(FX0, LY["3V"]), (V3_L, LY["3V"])], C["3V"], dot_start=True, dot_end=True)
wire([(FX0, LY["GND"]), (GND_L, LY["GND"])], C["GND"], dot_start=True, dot_end=True)
wire([(V3_L, BUS_TOP), (V3_L, LY["3V"])], C["3V"], lw=3)
wire([(GND_L, BUS_TOP + 2), (GND_L, LY["MI"] - 2.8)], C["GND"], lw=3)
# Carry both across the top to the right side
wire([(V3_L, BUS_TOP), (V3_R, BUS_TOP), (V3_R, RY["SDA"] - 4.6)], C["3V"], lw=3)
wire([(GND_L, BUS_TOP + 2), (GND_R, BUS_TOP + 2), (GND_R, RY["SDA"] - 6.8)], C["GND"], lw=3)
ax.text(V3_L, BUS_TOP + 0.8, "3V", color=C["3V"], fontsize=8, ha="right", fontweight="bold")
ax.text(GND_L - 0.5, BUS_TOP + 2.8, "GND", color=C["GND"], fontsize=8, ha="right", fontweight="bold")

# ---------------------------------------------------------------- INMP441 (left, top)
mx = 16
module(4, LY["A2"] - 3, 13, LY["3V"] - LY["A2"] + 6, "INMP441 mic", "L/R tied to GND = left channel")
for key, lab, col in (("3V", "VDD", "3V"), ("GND", "GND", "GND"), ("A0", "SCK", "MIC_SCK"),
                      ("A1", "WS", "MIC_WS"), ("A2", "SD", "MIC_SD")):
    mpin(17, LY[key], lab, "right")
    if key == "3V":
        wire([(17, LY[key]), (V3_L, LY[key])], C[col], dot_end=True)
    elif key == "GND":
        wire([(17, LY[key]), (GND_L, LY[key])], C[col], dot_end=True)
    else:
        wire([(17, LY[key]), (FX0, LY[key])], C[col])
mpin(17, LY["NC"], "L/R", "right")
wire([(17, LY["NC"]), (GND_L, LY["NC"])], C["GND"], dot_end=True)

# ---------------------------------------------------------------- PTT button (left)
py = LY["A5"]
wire([(FX0, py), (26, py)], C["PTT"])
ax.plot([26, 26], [py, py - 0.0], color="k")
ax.add_patch(Rectangle((20.5, py - 1.6), 5, 3.2, fc="#333", ec="k", zorder=4))
ax.add_patch(Circle((23, py), 1.0, fc="#b03030", ec="k", zorder=5))
ax.text(23, py + 2.2, "PTT button", ha="center", fontsize=9, fontweight="bold")
wire([(20.5, py), (18, py), (18, py - 2.2), (GND_L, py - 2.2)], C["GND"], dot_end=True)

# ---------------------------------------------------------------- Encoder (left, bottom)
ey0 = LY["MI"] - 4
module(4, ey0, 13, LY["SCK"] - ey0 + 1.6, "EC11 encoder", "C and one SW leg to GND", title_below=True)
for key, lab, col in (("SCK", "A", "ENC_A"), ("MO", "B", "ENC_B"), ("MI", "SW", "ENC_SW")):
    mpin(17, LY[key], lab, "right")
    wire([(17, LY[key]), (FX0, LY[key])], C[col])
mpin(17, ey0 + 1.2, "C + SW2", "right")
wire([(17, ey0 + 1.2), (GND_L, ey0 + 1.2)], C["GND"], dot_end=True)

# ---------------------------------------------------------------- timing test point RX/7
wire([(FX0, LY["RX"]), (41.5, LY["RX"])], C["TP"], lw=1.6)
ax.add_patch(Circle((41, LY["RX"]), 0.7, fc="white", ec=C["TP"], lw=1.6, zorder=5))
ax.text(40, LY["RX"] + 1.3, "TP capture", ha="right", fontsize=6.5, color="#555")

# ---------------------------------------------------------------- Slide switch on EN (right)
sy = RY["EN"]
wire([(FX1, sy), (65.8, sy)], C["EN"])
ax.add_patch(Rectangle((65.8, sy - 1.1), 4.6, 2.2, fc="#444", ec="k", zorder=4))
ax.add_patch(Rectangle((67.0, sy - 0.6), 1.6, 1.2, fc="#ddd", ec="k", zorder=5))
wire([(70.4, sy), (GND_R, sy)], C["GND"], dot_end=True)
ax.text(68.1, sy + 1.4, "power switch", ha="center", va="bottom", fontsize=6.2, fontweight="bold")

# ---------------------------------------------------------------- MAX98357A (right)
ax0, ay0 = 84, RY["32"] - 4.2
module(ax0, ay0, 13, RY["BAT"] + 2 - ay0, "MAX98357A amp")
ax.text(ax0 + 8.0, (RY["33"] + RY["32"]) / 2, "GAIN: not\nconnected", ha="center", va="center", fontsize=6.3, color="#d8eadf")
amp = [("BAT", "VIN", "BAT"), ("27", "BCLK", "AMP_BCLK"), ("33", "LRC", "AMP_LRC"), ("32", "DIN", "AMP_DIN")]
for key, lab, col in amp:
    y = RY[key]
    mpin(ax0, y, lab, "left")
    wire([(FX1, y), (ax0, y)], C[col], dot_start=False)
# amp GND
gy = RY["USB"] - 0.0
mpin(ax0, gy, "GND", "left")
wire([(ax0, gy), (GND_R, gy)], C["GND"], dot_end=True)
# amp SD from TX/8 (left header) routed under the board
sdy = ay0 + 1.4
mpin(ax0, sdy, "SD", "left")
wire([(FX0, LY["TX"]), (42, LY["TX"]), (42, -9), (82, -9), (82, sdy), (ax0, sdy)], C["AMP_SD"])
ax.text(61, -8.2, "TX / 8  to  amp SD (high = on)", fontsize=7.5, color="#6b6b10", ha="center")
# 470 uF cap across VIN-GND
ax.plot([80, 80], [RY["USB"], RY["BAT"]], color="#444", lw=1.4, zorder=5)
ax.add_patch(Rectangle((79.1, RY["USB"] + 1.4), 1.8, 3.2, fc="#2c4f9e", ec="k", zorder=6))
ax.add_patch(Circle((80, RY["BAT"]), 0.45, color="#444", zorder=6))
ax.add_patch(Circle((80, RY["USB"]), 0.45, color="#444", zorder=6))
ax.text(81.3, RY["USB"] + 3.0, "470uF", fontsize=6.5, ha="left", va="center")
# speaker
spx, spy = ax0 + 18, (RY["27"] + RY["33"]) / 2
ax.add_patch(Circle((spx, spy), 4.5, fc="#222", ec="k", zorder=4))
ax.add_patch(Circle((spx, spy), 2.0, fc="#555", ec="k", zorder=5))
ax.text(spx, spy + 5.6, "Speaker 4 ohm", ha="center", fontsize=9, fontweight="bold")
mpin(ax0 + 13, spy + 1.2, "+", "right")
mpin(ax0 + 13, spy - 1.2, "-", "right")
wire([(ax0 + 13, spy + 1.2), (spx - 4.4, spy + 1.2)], "#d62728", lw=1.8)
wire([(ax0 + 13, spy - 1.2), (spx - 4.4, spy - 1.2)], "#1f77b4", lw=1.8)
ax.text(spx + 3.0, spy - 6.0, "both speaker wires go ONLY\nto the amp's + and - terminals.\nNeither one is ground.", ha="center", va="top", fontsize=7, color="#b00000")

# ---------------------------------------------------------------- timing test points 13/15/14 (right)
for key, lab in (("13", "TP TX LED"), ("15", "TP encode"), ("14", "TP playback")):
    y = RY[key]
    wire([(FX1, y), (67.5, y)], C["TP"], lw=1.6)
    ax.add_patch(Circle((68, y), 0.7, fc="white", ec=C["TP"], lw=1.6, zorder=5))
    ax.text(68, y - 1.5, lab, ha="center", fontsize=6, color="#555")

# ---------------------------------------------------------------- OLED (right, bottom)
OX = 99
oy0 = RY["SDA"] - 8
module(OX, oy0, 14, RY["SCL"] + 1.8 - oy0, "SSD1306 OLED", "I2C address 0x3C", title_below=True)
for key, lab, col in (("SCL", "SCL", "SCL"), ("SDA", "SDA", "SDA")):
    y = RY[key]
    mpin(OX, y, lab, "left")
    wire([(FX1, y), (OX, y)], C[col])
mpin(OX, RY["SDA"] - 4.6, "VCC", "left")
wire([(OX, RY["SDA"] - 4.6), (V3_R, RY["SDA"] - 4.6)], C["3V"], dot_end=True)
mpin(OX, RY["SDA"] - 6.8, "GND", "left")
wire([(OX, RY["SDA"] - 6.8), (GND_R, RY["SDA"] - 6.8)], C["GND"], dot_end=True)

# ---------------------------------------------------------------- battery
ax.add_patch(FancyBboxPatch((70, BUS_TOP + 5), 16, 5, boxstyle="round,pad=0,rounding_size=1", fc="#c9c9c9",
                            ec="#555"))
ax.text(78, BUS_TOP + 7.5, "LiPo 3.7 V 1200 mAh", ha="center", va="center", fontsize=8)
wire([(70, BUS_TOP + 7.5), (FX1 - 3, BUS_TOP + 7.5), (FX1 - 3, TOP + 1.6)], "#d62728", lw=1.6)
ax.text(87.5, BUS_TOP + 7.5, "plugs into the Feather's\nJST socket, no soldering", fontsize=7, ha="left", va="center", color="#444")

ax.text(-1, 90, "Walkie talkie wiring, one unit", fontsize=15, fontweight="bold")
ax.text(-1, 86.8, "Yellow pads = used pins. Dots = joined to a bus.",
        fontsize=9, color="#444")

ax.text(-1, 84.0, "Power switch: EN to GND turns the unit off. The battery still charges over USB-C.", fontsize=8, color="#333")
ax.text(-1, 81.6, "Timing test points (TP) are optional probe spots for a logic analyzer.", fontsize=8, color="#333")
plt.savefig("docs/wiring.png", bbox_inches="tight", facecolor="white")
plt.savefig("docs/wiring.svg", bbox_inches="tight", facecolor="white")
print("wrote docs/wiring.png and docs/wiring.svg")
