# Hardware Wiring

Complete wiring for the Pico modified square-wave push-pull inverter. GPIO
numbers match `src/board.h`; physical pin numbers are for the Pico 2020
(RP2040) board, USB connector at the top, pin 1 top-left.

**Read the safety section before connecting the 12 V power stage.** The
transformer output can be lethal.

---

## 1. Block diagram

```
                        +5 V (USB, or 12 V -> 5 V buck)
                                  |
                                  v
                    +-----------------------------+
   OLED 1.3" <=====>| GP0/GP1  (I2C0)             |
   TSOP1838  ------>| GP2      (IR input)         |
                    | GP6  ---> channel A driver  |---> 5x IRF540 ---+
                    | GP7  ---> channel B driver  |---> 5x IRF540 ---+--> transformer
   setup jumper --->| GP14     (to GND)           |
                    | GP25 --- onboard LED        |
                    +-----------------------------+
```

Signal ground and power ground are separate only if the Pico is powered from an
isolated 12 V -> 5 V converter. Otherwise `GND_LOGIC` and `GND_PWR` are the same
net and must meet at one star point.

---

## 2. Pico header connections

| Physical pin | GPIO | Function | Connects to |
|---:|---|---|---|
| 1 | GP0 | I2C0 SDA | OLED SDA |
| 2 | GP1 | I2C0 SCL | OLED SCL |
| 3 | GND | Logic ground | OLED GND, TSOP1838 GND |
| 4 | GP2 | IR input | TSOP1838 OUT |
| 8 | GND | Logic ground | IR receiver ground return |
| 9 | GP6 | Side A drive | 220 R -> PC123-A pin 1 |
| 10 | GP7 | Side B drive | 220 R -> PC123-B pin 1 |
| 18 | GND | Logic ground | setup jumper return |
| 19 | GP14 | Setup | jumper to GND (close at power-up) |
| 36 | 3V3(OUT) | 3.3 V | OLED VCC, TSOP1838 VS, pull-ups |
| 38 | GND | Logic ground | 5 V supply return |
| 39 | VSYS | 5 V input | 5 V buck output (never more than 5.5 V) |
| 40 | VBUS | USB 5 V | USB connector (optional) |

GP25 is the on-board LED and is not brought out to the header; no wiring is
needed for it.

---

## 3. Display and IR receiver (logic side)

```
   Pico 3V3 (36) ----+----------------------+---- OLED VCC
                     |                      |
                   100nF                   === (module pull-ups,
                     |                      |   if fitted)
   Pico GND ---------+----------------------+---- OLED GND
                     |
   Pico GP0 (1) -------------------------------- OLED SDA
   Pico GP1 (2) -------------------------------- OLED SCL


   Pico 3V3 (36) ---- 100R ----+---- TSOP1838 pin 3 (VS)
                               |
                             100nF
                               |
   Pico GND -------------------+---- TSOP1838 pin 2 (GND)

   Pico 3V3 (36) ---- 10k ------------------+
                                            |
   Pico GP2 (4) -----------------------------+---- TSOP1838 pin 1 (OUT)
```

- TSOP1838 pin order, viewed from the front: **1 = OUT, 2 = GND, 3 = VS**.
- Most 1.3 inch OLED modules have on-board I2C pull-ups. If yours does not,
  add 4.7 k from SDA and SCL to 3V3. The firmware also enables the internal
  pull-ups.
- Module pin order varies: check the silkscreen (`GND VCC SCL SDA` is common).
- Use an SH1106 build for most 1.3 inch modules, SSD1306 with
  `-DDISPLAY_SSD1306=1`.

---

## 4. One gate-drive channel (build twice)

`x` below means the side: `A` uses GP6, `B` uses GP7. Use separate PC123,
BD139, BD140 and resistors for each side. Never share a driver between sides.

```
  LED INPUT
  =========
  Pico GP6 or GP7 ---- 220R ---- PC123 pin 1 (anode)
                                 PC123 pin 2 (cathode) ---- GND_LOGIC

  ISOLATED OUTPUT
  ===============
  +12V_ISO ------------------- PC123 pin 4 (collector)
                               PC123 pin 3 (emitter) ---- node X
  node X ---- 4.7k ---- GND_ISO
  node X ---- 470R ---- base bus
```

```
  TOTEM POLE  (base bus = BD139 base = BD140 base)

                        +12V_ISO
                           |
                        +--+--+
                        |  C  |
           base bus ----| B   |
                        |  E  |----+
                        +-----+    |
                         BD139     |
                         (NPN)     |
                                   +------ gate bus
                        +-----+    |
           base bus ----| B   |    |
                        |  E  |----+
                        |  C  |
                        +--+--+
                           |
                        GND_ISO
                         BD140
                         (PNP)
```

Net summary, channel A (same for B with GP7 and PC123-B):

| From | To |
|---|---|
| GP6 | 220 R |
| 220 R | PC123-A pin 1 |
| PC123-A pin 2 | GND_LOGIC |
| PC123-A pin 4 | +12V_ISO |
| PC123-A pin 3 | node X |
| node X | 4.7 k to GND_ISO |
| node X | 470 R to base bus |
| base bus | BD139 base and BD140 base |
| BD139 collector | +12V_ISO |
| BD140 collector | GND_ISO |
| BD139 emitter + BD140 emitter | gate bus |

**Why this is fail-safe:** GPIO low or Pico reset -> LED off -> PC123 off ->
4.7 k pulls node X low -> BD140 (PNP) conducts -> gate bus low. A missing or
open optocoupler also leaves the gate bus low. A shorted optocoupler is the one
fault this circuit cannot tolerate; fit a fuse and a physical kill switch.

BD139/BD140 pinout (front, flat side toward you, legs down): **C B E** is not
universal; check the datasheet or measure. A common TO-126 pinout is
**E C B** (emitter left). Wire by the datasheet, not by position.

PC123 pinout: **1 anode, 2 cathode, 3 emitter, 4 collector**.

---

## 5. MOSFET output stage

Five IRF540s in parallel per side. Repeat the gate network for every FET; do
not use one gate resistor for all five.

```
  gate bus ---- 10R ---- gate ----+---- G  +----------+  D ----+---- drain bus
                                  |        | IRF540   |        |
                                 10k       |    x5    |        |
                                  |        |          |        |
                                15V zener  +---- S ---+--------+---- source bus
                                  |             (GND_PWR)
                                source
```

Per MOSFET:

| Terminal | Connects to |
|---|---|
| Gate | one 10 R from the gate bus, one 10 k to source, one 15 V zener to source |
| Drain | common drain bus for that side |
| Source | common source bus / `GND_PWR` |

- Use a 15 V zener (BZX55C15 or similar) gate-source, cathode to gate, anode
  to source. IRF540's Vgs maximum is +/-20 V; the zener also protects against
  driver faults.
- Optional: 0.01 R / 5 W source ballast per FET for current sharing. If used,
  connect it between the FET source and the common source bus, and take the
  10 k and zener to the FET source (before the ballast).
- Insulate the tabs from the heatsink or use one shared insulated mounting.
  The drain tab is live.
- Add a TVS (1.5KE36A/39A) and/or a 100 R + 100 nF snubber across each
  drain-source, close to the FETs.
- Add 100 uF + 100 nF from `+12V_ISO` to `GND_ISO` at each totem pole, and
  large bulk capacitance near the transformer centre tap.

Drain buses:

```
  Side A: 5 drains ----+------------------------------- transformer primary end 1

  Side B: 5 drains ----+------------------------------- transformer primary end 2
```

---

## 6. Power stage

```
  BATTERY + ----[ FUSE ]----[ KILL SWITCH ]----+------------------+
                                               |                  |
                                              +++               centre tap
                                     1000uF   ---               of primary
                                     100nF    +++                  |
                                               |                  |
                                             GND_PWR              |
                                                                  |
                                        +-------------------------+
                                        |
                                  +-----+-----+
                                  |  PRIMARY  |   12-0-12 V
                                  +-----+-----+
                                        |
                          +-------------+-------------+
                          |                           |
                 end 1 ---+                           +--- end 2
                          |                           |
                    Side A drains               Side B drains
```

- The centre tap goes to the battery positive through the fuse and kill
  switch. Side A and side B each switch one outer end to ground.
- Transformer example: 12-0-12 V primary, 220 V secondary. The primary
  current and MOSFET rating depend on your transformer; size the fuse for the
  transformer, not for the MOSFETs.
- The drain voltage is about 2 x 12 V plus leakage spike. IRF540 is a 100 V
  part; the TVS/snubber is there to keep the spike within that rating.
- Add a bleeder resistor across the bulk capacitor and a fuse on the mains
  side of the transformer if you build a mains-output unit.

Pico supply, isolated option:

```
  +12V_ISO ----+---- [ 12 V -> 5 V isolated DC-DC ] ---- 5 V ---- VSYS (39)
               |                                               |
            GND_ISO                                        GND_LOGIC (38)
```

Non-isolated option: use a 12 V -> 5 V buck and tie the buck output ground to
`GND_PWR` at the battery negative star point. VSYS must never exceed 5.5 V.

---

## 7. Grounding and layout

```
  Battery -------+-------------------+-------------------+
                 |                   |                   |
             bulk cap            FET sources        logic 5 V return
             (GND_PWR)           (GND_PWR)          (GND_LOGIC*)
```

`*` separate only when using an isolated DC-DC; otherwise join at the battery
negative star point.

- Keep the high-current loop (battery + -> fuse -> centre tap -> primary ->
  MOSFET -> source -> battery -) short and twisted.
- Run the gate bus and the source return as a twisted pair to each FET group.
- Keep the OLED/IR wiring away from the drain and transformer leads.
- Use one star point for power ground; do not daisy-chain MOSFET source
  returns through the logic ground.
- The optocoupler only provides galvanic isolation if the two supplies are
  actually isolated. With a shared ground it still provides level shifting and
  the fail-safe behaviour.

---

## 8. Safety

- The secondary side can be at mains potential. Do not touch it while powered.
- Do not connect an earth-referenced oscilloscope to the mains output. Use a
  differential probe or an isolation transformer.
- Test the logic and gate-drive stages with the transformer disconnected and a
  current-limited 12 V supply first.
- Fit a fuse, a physical kill switch, and a bleeder resistor.
- The firmware has no current sense, over-temperature shutdown, or
  desaturation protection. A shorted optocoupler or a failed driver can leave
  a MOSFET on. Add hardware protection for a permanent installation.
