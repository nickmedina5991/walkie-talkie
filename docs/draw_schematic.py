"""Schematic for one walkie talkie unit, drawn with net labels.

Run:  pip install schemdraw && python3 docs/draw_schematic.py  ->  docs/schematic.png / .svg
Two pins with the same net label are connected.
"""
import schemdraw
schemdraw.use("matplotlib")
import schemdraw.elements as elm

schemdraw.config(fontsize=11, lw=1.3)

STUB = 1.2


def net(d, anchor, name, direction):
    """Short wire from a pin to a net label."""
    line = elm.Line().at(anchor).length(STUB)
    line = getattr(line, direction)()
    d.add(line)
    loc = {"left": "left", "right": "right", "up": "top", "down": "bottom"}[direction]
    d.add(elm.Label().at(line.end).label(name, loc=loc, fontsize=10))


def gnd(d, anchor, direction="down"):
    line = elm.Line().at(anchor).length(0.8)
    d.add(getattr(line, direction)())
    d.add(elm.Ground().at(line.end))


def ic(d, label, pins, at, size, pinspacing=0.75, edgepadH=0.5, edgepadW=0.5):
    # schemdraw stacks side pins bottom-up; reverse so the list reads top-down on the page.
    pins = [p for p in reversed(pins) if p[1] in ("left",)] + [p for p in reversed(pins) if p[1] == "right"]
    e = elm.Ic(pins=[elm.IcPin(name=n, side=s, anchorname=a) for (n, s, a) in pins], size=size,
               pinspacing=pinspacing, edgepadH=edgepadH, edgepadW=edgepadW, plblsize=10)
    e = e.at(at).theta(0).label(label, loc="top", fontsize=12, ofst=0.3)  # theta(0): ignore last wire direction
    return d.add(e)


with schemdraw.Drawing(file=None, show=False) as d:
    d.config(unit=2.5)

    # U1 Feather V2 ---------------------------------------------------------
    u1_pins = [
        ("3V", "left", "V3"), ("GND", "left", "GND"), ("BAT", "left", "BAT"), ("EN", "left", "EN"),
        ("USB", "left", "USB"),
        ("A0/GPIO26", "left", "A0"), ("A1/GPIO25", "left", "A1"), ("A2/GPIO34", "left", "A2"),
        ("A5/GPIO4", "left", "A5"), ("SCK/GPIO5", "left", "SCK"), ("MO/GPIO19", "left", "MO"),
        ("MI/GPIO21", "left", "MI"),
        ("GPIO27", "right", "D27"), ("GPIO33", "right", "D33"), ("GPIO32", "right", "D32"),
        ("TX/GPIO8", "right", "TX"), ("SDA/GPIO22", "right", "SDA"), ("SCL/GPIO20", "right", "SCL"),
        ("RX/GPIO7", "right", "RX"), ("GPIO13", "right", "D13"), ("GPIO14", "right", "D14"),
        ("GPIO15", "right", "D15"), ("JST+", "right", "JSTP"), ("JST-", "right", "JSTN"),
    ]
    u1 = ic(d, "U1  Adafruit ESP32 Feather V2", u1_pins, (0, 0), (6, 9.5), pinspacing=0.75)
    net(d, u1.V3, "+3V3", "left")
    gnd(d, u1.GND, "left") if False else net(d, u1.GND, "GND", "left")
    net(d, u1.BAT, "VBAT", "left")
    net(d, u1.EN, "EN", "left")
    d.add(elm.Line().at(u1.USB).left(0.6))
    d.add(elm.NoConnect())
    net(d, u1.A0, "MIC_SCK", "left")
    net(d, u1.A1, "MIC_WS", "left")
    net(d, u1.A2, "MIC_SD", "left")
    net(d, u1.A5, "PTT", "left")
    net(d, u1.SCK, "ENC_A", "left")
    net(d, u1.MO, "ENC_B", "left")
    net(d, u1.MI, "ENC_SW", "left")
    net(d, u1.D27, "AMP_BCLK", "right")
    net(d, u1.D33, "AMP_LRC", "right")
    net(d, u1.D32, "AMP_DIN", "right")
    net(d, u1.TX, "AMP_SD", "right")
    net(d, u1.SDA, "I2C_SDA", "right")
    net(d, u1.SCL, "I2C_SCL", "right")
    for a, name in ((u1.RX, "TP1 capture"), (u1.D13, "TP2 TX LED"), (u1.D14, "TP3 playback"),
                    (u1.D15, "TP4 encode")):
        ln = d.add(elm.Line().at(a).right(STUB))
        d.add(elm.Dot(open=True).at(ln.end).label(name, loc="right", fontsize=10))
    # Battery on the JST connector
    lp = d.add(elm.Line().at(u1.JSTP).right(2.4))
    ln = d.add(elm.Line().at(u1.JSTN).right(2.4))
    bat = d.add(elm.BatteryCell().theta(0).endpoints(lp.end, ln.end).label("BT1 LiPo\n3.7 V 1200 mAh", loc="bot", ofst=0.4))

    # U2 INMP441 -------------------------------------------------------------
    u2 = ic(d, "U2  INMP441 mic", [("VDD", "right", "VDD"), ("GND", "right", "GND"), ("L/R", "right", "LR"),
                                  ("SCK", "right", "SCK"), ("WS", "right", "WS"), ("SD", "right", "SD")],
            (-14, 2.2), (2.6, 4.6))
    net(d, u2.VDD, "+3V3", "right")
    net(d, u2.GND, "GND", "right")
    net(d, u2.LR, "GND", "right")
    net(d, u2.SCK, "MIC_SCK", "right")
    net(d, u2.WS, "MIC_WS", "right")
    net(d, u2.SD, "MIC_SD", "right")

    # SW1 encoder, SW2 PTT, SW3 power ------------------------------------------
    u4 = ic(d, "SW1  EC11 rotary encoder", [("A", "right", "A"), ("B", "right", "B"), ("C", "right", "C"),
                                            ("SW1", "right", "S1"), ("SW2", "right", "S2")],
            (-14, -5.2), (2.6, 4.0))
    net(d, u4.A, "ENC_A", "right")
    net(d, u4.B, "ENC_B", "right")
    net(d, u4.C, "GND", "right")
    net(d, u4.S1, "ENC_SW", "right")
    net(d, u4.S2, "GND", "right")

    ptt = d.add(elm.Button().at((-15.5, -9.0)).theta(0).label("SW2  PTT", loc="top"))
    d.add(elm.Line().at(ptt.end).right(0.4))
    d.add(elm.Label().label("PTT", loc="right", fontsize=10))
    d.add(elm.Line().at(ptt.start).left(0.4))
    d.add(elm.Label().label("GND", loc="left", fontsize=10))

    pwr = d.add(elm.Switch().at((-15.5, -11.6)).theta(0).label("SW3  power (slide)", loc="top"))
    d.add(elm.Line().at(pwr.end).right(0.4))
    d.add(elm.Label().label("EN", loc="right", fontsize=10))
    d.add(elm.Line().at(pwr.start).left(0.4))
    d.add(elm.Label().label("GND", loc="left", fontsize=10))

    # U3 MAX98357A + speaker --------------------------------------------------
    u3 = ic(d, "U3  MAX98357A amp", [("VIN", "left", "VIN"), ("GND", "left", "GND"), ("SD", "left", "SD"),
                                    ("GAIN", "left", "GAIN"), ("DIN", "left", "DIN"), ("BCLK", "left", "BCLK"),
                                    ("LRC", "left", "LRC"), ("OUT+", "right", "OP"), ("OUT-", "right", "ON")],
            (15, 2.4), (2.8, 5.4))
    net(d, u3.VIN, "VBAT", "left")
    net(d, u3.GND, "GND", "left")
    net(d, u3.SD, "AMP_SD", "left")
    d.add(elm.Line().at(u3.GAIN).left(0.6))
    d.add(elm.NoConnect())
    net(d, u3.DIN, "AMP_DIN", "left")
    net(d, u3.BCLK, "AMP_BCLK", "left")
    net(d, u3.LRC, "AMP_LRC", "left")
    op = d.add(elm.Line().at(u3.OP).right(1.0))
    on = d.add(elm.Line().at(u3.ON).right(1.0))
    d.add(elm.Speaker().at(op.end).anchor("in1").theta(0).label("LS1\n4 ohm 3 W", loc="right"))

    c1 = d.add(elm.Capacitor(polar=True).at((15.3, -1.0)).down().label("C1\n470 uF", loc="bot"))
    d.add(elm.Line().at(c1.start).up(0.4))
    d.add(elm.Label().label("VBAT", loc="top", fontsize=10))
    d.add(elm.Line().at(c1.end).down(0.4))
    d.add(elm.Ground())
    d.add(elm.Label().at((16.0, -3.3)).label("(place at amp VIN)", fontsize=9, halign="left"))

    # U4 OLED ------------------------------------------------------------------
    u5 = ic(d, "U4  SSD1306 OLED 128x64", [("VCC", "left", "VCC"), ("GND", "left", "GND"), ("SCL", "left", "SCL"),
                                           ("SDA", "left", "SDA")],
            (15, -8.6), (2.8, 3.2))
    net(d, u5.VCC, "+3V3", "left")
    net(d, u5.GND, "GND", "left")
    net(d, u5.SCL, "I2C_SCL", "left")
    net(d, u5.SDA, "I2C_SDA", "left")

    d.add(elm.Label().at((-15.5, 11.2)).label("ESP32 walkie talkie, one unit", fontsize=16, halign="left"))
    d.add(elm.Label().at((-15.5, 10.3)).label(
        "Same net label = connected. GPIO numbers are ESP32 numbers; Feather silkscreen names in front.",
        fontsize=10, halign="left"))
    d.add(elm.Label().at((-15.5, -14.2)).label(
        "Feather V2 on-board parts not drawn: USB-C LiPo charger, AP2112K 3.3 V regulator, 200k/200k battery divider "
        "on GPIO35, red LED on GPIO13.", fontsize=9, halign="left"))
    d.add(elm.Label().at((-15.5, -15.0)).label(
        "Avoid GPIO12 (strapping), GPIO16/17 (PSRAM), GPIO0/2 (NeoPixel). Encoder and buttons use internal pull-ups.",
        fontsize=9, halign="left"))

    d.save("docs/schematic.svg")
    d.save("docs/schematic.png", dpi=150)
print("wrote docs/schematic.png and docs/schematic.svg")
