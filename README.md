# Pico Modified Square-Wave Push-Pull Inverter

Firmware for a Raspberry Pi Pico (RP2040) driving a two-channel push-pull
inverter through PC123 optocouplers and BD139/BD140 totem-pole gate drivers.

The Pico generates the modified square wave, reads a TSOP1838 IR receiver,
shows frequency / duty / output state on a 1.3 inch I2C OLED, and learns the
remote-control buttons on first boot.

## Features

- 1 to 60 Hz adjustable frequency; every cycle is identical (fixed integer half period).
- DUTY is the per-channel duty cycle over the **full period**, exactly what a scope
  shows on GP6/GP7. PWM learning is stored in flash.
- Side A on -> both off -> Side B on -> both off, symmetric layout, identical gaps.
- Minimum 200 us dead time on both edges, enforced by clamping.
- Volt-second guard: 10 ms max ON pulse (`LIM` shown) for low frequency.
- PWM ON applies the full wave immediately (no soft-start ramp by default; re-enable with `-DPWM_SOFT_START=1`); immediate PWM OFF; boots with output OFF.
- IR remote learn/setup wizard stored in flash.
- 7 assignable buttons: FRQ+, FRQ-, DUTY+, DUTY-, PWM ON, PWM OFF, RESET ALL.
- SH1106 OLED by default; SSD1306 build option.
- Minimalist landscape dashboard with 3x bold FRQ/DUTY zones and an animated full-width PWM status bar; no on-screen telemetry.
- 1 second watchdog: a hung Pico resets and turns both sides off.

## Important timing rule

```
half   = (1 / frequency) / 2
dead   = 200 us
period = 2 * half
on     = period * duty / 100    (clamped to half - dead and to 10 ms)
off    = half - on              (identical gap on both edges)
```

At 50 Hz:

| DUTY | ON time | Both-OFF time | Scope per channel |
|---|---:|---:|---:|
| 5 % | 1.0 ms | 9.0 ms | 5 % |
| 25 % | 5.0 ms | 5.0 ms | 25 % |
| 49 % | 9.8 ms | 200 us | 49 % |
| 50 % | 9.8 ms | 200 us | 49.5 % (LIM) |

At 60 Hz the duty saturates the same way at 48.8 % (8133 us on, 200 us gap).
`LIM` is shown whenever the dead-time or volt-second clamp is active.

> The OLED percentage is the per-channel duty of the *full period*. Exact 50 %
> per channel plus dead time is impossible (the two ON times would already fill
> the period), so the top of the DUTY range saturates just below 50 % with LIM.

If you want the exact complementary 50.000 % square wave with **zero dead time**
(bench test only), build the hardware-PWM variant:

```powershell
cmake -S . -B build-exact50 -G Ninja -DPICO_NO_PICOTOOL=1 -DPWM_EXACT_50=1
cmake --build build-exact50
```

That variant produces `8333 us` on GP6, `8333 us` on GP7, exactly 50 %, at
60.0024 Hz, with DUTY locked. Never connect the MOSFET stage to it.

### Low-frequency transformer guard

A normal 50/60 Hz transformer cannot accept a 500 ms half-cycle at 1 Hz. The
firmware caps the ON pulse at `PWM_MAX_ON_US` (10 ms in `src/board.h`). At
50/60 Hz this has no effect. At lower frequencies the display shows `LIM` and
the actual pulse is shorter than the requested duty. This protects the
transformer and MOSFETs. Change `PWM_MAX_ON_US` only if your transformer is
genuinely rated for the lower frequency.

## Pin map

| Pico pin | Function |
|---|---|
| GP0 | OLED SDA |
| GP1 | OLED SCL |
| GP2 | TSOP1838 OUT |
| GP6 | Side A optocoupler LED |
| GP7 | Side B optocoupler LED |
| GP14 | Setup jumper to GND (optional) |
| GP25 | Onboard status LED |
| 3V3 | OLED and TSOP1838 supply |
| GND | Signal ground |

## Wiring

### OLED and IR receiver

```
Pico 3V3  -> OLED VCC
Pico GND  -> OLED GND
Pico GP0  -> OLED SDA
Pico GP1  -> OLED SCL

Pico 3V3  -> TSOP1838 VCC
Pico GND  -> TSOP1838 GND
Pico GP2  -> TSOP1838 OUT
10k from GP2 to 3V3
100nF from TSOP VCC to GND
```

Most 1.3 inch 128x64 modules use the SH1106 controller. If your module is an
SSD1306, build with `-DDISPLAY_SSD1306=1`.

### One gate-drive channel

Repeat this for each side. The component values below assume a 3.3 V Pico
output and a separate 12 V gate-drive rail.

```
Pico GP6 or GP7 -- 220R -- PC123 pin 1 (LED anode)
PC123 pin 2 (LED cathode) -- GND

+12V_ISO -- PC123 pin 4 (collector)
PC123 pin 3 (emitter) -- node X
node X -- 4.7k -- GND
node X -- 470R -- common base of BD139 and BD140

BD139 NPN:
  collector -> +12V_ISO
  emitter   -> gate bus
BD140 PNP:
  collector -> GND_ISO
  emitter   -> gate bus

gate bus -- 10R -- each IRF540 gate
each IRF540 gate -- 10k -- source/GND
each IRF540 gate -- 15V zener -- source/GND   (recommended)
```

This is deliberately fail-safe:

- Pico reset or GPIO low -> PC123 LED off -> node X low -> BD140 on -> gate low.
- Optocoupler removed or failed open -> 4.7k pulls node X low -> gate low.
- Gate pull-down resistors keep the MOSFETs off if the driver is disconnected.

Do not reverse the BD139/BD140 arrangement. The PC123 emitter-follower drives
the common bases; the NPN is the high-side pull-up and the PNP is the low-side
pull-down. Only the BD139/BD140 base current flows through the PC123, so keep
it below about 25 mA (470 ohm at 12 V). The MOSFET gate current comes from the
12 V rail through the BD139/BD140 collectors.

### Five parallel IRF540s per side

- Use one 10 ohm gate resistor per MOSFET.
- Use one 10 kohm gate-source resistor per MOSFET.
- Add a 15 V gate-source zener per MOSFET if possible.
- Add a 0.01 ohm / 5 W source ballast resistor per MOSFET for current sharing
  if you have them.
- Common sources go to the power ground star point.
- Common drains go to one end of the centre-tapped transformer primary.
- Add a TVS clamp across each drain-source, for example 1.5KE36A/39A.
- Add an RC snubber across each drain-source, for example 100 ohm + 100 nF.
- Add 100 uF plus 100 nF local decoupling at each totem pole.
- Add large bulk capacitance across the 12 V feed near the transformer centre
  tap.

### Power and isolation

- Fuse the battery positive feed and fit a physical kill switch.
- The Pico side and the 12 V inverter side should use separate supplies if you
  want true galvanic isolation. A small isolated 12 V -> 5 V DC-DC module is a
  good choice for the Pico.
- If the Pico and the 12 V inverter share a ground, the PC123s still provide
  level shifting and fault protection, but not isolation.
- Keep gate-drive wires short and twisted. Keep the high-current loop
  (battery -> centre tap -> MOSFETs -> ground) as small as possible.
- Use a heatsink for the MOSFETs.

## Build

Install the Pico SDK, CMake, Ninja, and the Arm GNU toolchain. The easiest
Windows setup is the Raspberry Pi Pico VS Code extension, which installs the
toolchain and SDK.

Manual build:

```powershell
git clone https://github.com/raspberrypi/pico-sdk.git
$env:PICO_SDK_PATH = "C:\path\to\pico-sdk"

cmake -S . -B build -G Ninja
cmake --build build
```

The UF2 file is:

```
build/pico_inverter.uf2
```

Hold BOOTSEL, connect the Pico, and copy the UF2 to the RPI-RP2 drive.

The SDK normally builds `picotool` to create the UF2. If your machine has no
host C/C++ compiler, configure with `-DPICO_NO_PICOTOOL=1` and convert the
linked ELF to a UF2 with one command (the script locates `arm-none-eabi-objcopy`
on `PATH`):

```powershell
$env:PICO_SDK_PATH = "C:\path\to\pico-sdk"
$env:PATH = "C:\path\to\arm-none-eabi-gcc\bin;$env:PATH"
cmake -S . -B build -G Ninja -DPICO_NO_PICOTOOL=1
cmake --build build
python tools/bin2uf2.py build/pico_inverter.elf build/pico_inverter.uf2
```

For an SSD1306 display:

```powershell
cmake -S . -B build-ssd1306 -G Ninja -DDISPLAY_SSD1306=1
cmake --build build-ssd1306
```

The configuration is stored in the last two 4 kB flash sectors. The firmware
is small, so it does not overlap them. Do not add a large filesystem or a
binary larger than about 2 MB minus 8 kB without moving the configuration.

## First use: IR setup

On first boot, or if GP14 is jumpered to GND, the OLED shows:

```
IR SETUP
PRESS
FRQ+
1/7
```

Press the remote button you want for FRQ+. Repeat for:

1. FRQ+
2. FRQ-
3. DUTY+
4. DUTY-
5. PWM ON
6. PWM OFF
7. RESET ALL

Duplicate codes are rejected with `DUPLICATE / TRY ANOTHER`. After the seventh
button the configuration is saved to flash.

To re-run setup later:

- Press the **RESET ALL** button, or
- Hold **PWM OFF** on the main screen for about 3 seconds, or
- Power up with GP14 connected to GND.

`RESET ALL` is the remote equivalent of a factory reset: it erases every stored
button and starts the wizard again from `1/7`. The output is forced OFF first,
and the empty state is saved immediately, so pulling the power during the
wizard simply re-runs it on the next boot.

The receiver is a 38 kHz demodulating type (TSOP1838). The decoder supports
NEC, NEC repeat frames, and Samsung-style 4.5 ms/4.5 ms lead frames. Most
inexpensive remotes use one of these. If your remote uses another protocol,
the learned code will not work; the protocol decoder can be extended in
`src/ir_nec.c`.

## Main screen

Push-pull "stacked instrument" dashboard (128x64), rebuilt from scratch:
three hard, non-overlapping bands, values right-aligned on one shared edge,
zero telemetry:

```
+--------------------------------+
| FREQ HZ                   LIM  |
|                          6 0   |   24 px bold numerals
|--------------------------------|
| DUTY %                    2 5  |   14 px bold numerals
|+------------------------------+|
||##########···················|.|   PWM meter
|+------------------------------+|
+--------------------------------+
```

- **Band 1 (y 0..30)** — `FREQ HZ` caption plus the frequency value in
  **24 px bold numerals** from a real sans typeface, right-aligned on the
  value grid at x=122. `LIM` appears at the far right of the caption row only
  while a clamp is active.
- **Band 2 (y 33..48)** — a 1 px rule, then `DUTY %` and the duty value in
  **14 px bold numerals** on the same right edge.
- **Band 3 (y 50..63)** — the **full-width PWM meter**. It encodes the state
  without a single character:
  - output ON: fills to the duty ratio of the period, quarter ticks, a crisp
    leading edge, a travelling window (the only motion on the panel) and a
    pilot block at the end of the track that stays visible even at 1 % duty;
  - output OFF: empty trough with a dashed centre line.
- Numerals are rasterised from Segoe UI Bold by `tools/gen_digits.py` and
  stored as per-pixel coverage (`src/digit_data.c`), then ordered-dithered on
  their edges at draw time — the closest thing to anti-aliased typography a
  1-bit OLED can show. Captions keep the 5x7 face.
- No voltage, temperature, battery or other telemetry is drawn anywhere.
- `FRQ+` / `FRQ-`: 1 Hz steps, 1 to 60 Hz.
- `DUTY+` / `DUTY-`: 1 % steps, 1 to 100 %; saturates at ~49 % of the period
  (`LIM`); the scope per channel matches the OLED percentage.
- `PWM ON` / `PWM OFF`: immediate start/stop.
- `PWM OFF` held for 3 s: enter IR setup. `RESET ALL`: full factory
  re-learn wizard from `1/7`.

Preview the frame on a PC without hardware:

```
python tools/gen_digits.py            # regenerate src/digit_data.c
python tools/preview_dashboard.py 60 25 on    # -> build/dashboard_*.png
```


Settings are saved when the output is OFF. If you change frequency or duty
while the output is ON, the new value is held in RAM and written to flash when
you turn the output OFF. This avoids interrupting the output during a flash
write.

## Safety

This firmware controls a power stage that can produce lethal voltages and very
high currents.

- Test the logic and gate-drive stages without the transformer first.
- Use a current-limited 12 V supply and a dummy load for initial power tests.
- Fit a fuse and a physical kill switch.
- Do not work on the mains output while powered.
- Do not use an earth-referenced oscilloscope on the mains output. Use a
  differential probe or an isolation transformer.
- The firmware has no current-sense or over-temperature shutdown. Add a fuse,
  and consider adding a current sensor and heatsink thermostat for a permanent
  high-power installation.
- Low-frequency operation at high power is limited by the transformer, not by
  the firmware. The `LIM` indicator means the pulse is being capped.

## Source layout

| File | Purpose |
|---|---|
| `src/main.c` | Init and main loop |
| `src/waveform.c` | Timer alarm waveform, dead time, soft start (`-DPWM_TIMER_DEADTIME=1`) |
| `src/waveform_hw.c` | Default: one hardware PWM slice, exact complementary 50 % |
| `src/ir_nec.c` | NEC IR edge decoder |
| `src/display.c` | SH1106/SSD1306 I2C driver |
| `src/font.c` | Generated 5x7 font |
| `src/ui.c` | Setup wizard and main screen |
| `src/config_flash.c` | Two-slot flash configuration |
| `src/board.h` | Pins and timing constants |
| `tools/gen_font.py` | Regenerates `src/font.c` |
| `tools/bin2uf2.py` | Converts the flash `.bin` to a UF2 without picotool |

## Troubleshooting

**Display blank**
- Check 3.3 V and GND at the module.
- Check SDA/SCL are on GP0/GP1 and the module has pull-ups.
- Try the other address (the firmware probes 0x3C then 0x3D).
- If the image is shifted or garbled, rebuild with `-DDISPLAY_SSD1306=1`.

**IR does not learn**
- Check the TSOP1838 is powered from 3.3 V and has a 10 k pull-up on OUT.
- Use a 38 kHz NEC remote.
- The USB serial output prints learned codes and duplicate warnings.
- If the remote is not NEC, add its protocol to `src/ir_nec.c`.

**No gate output**
- Confirm the main screen shows `PWM ON`.
- Check the PC123 LED current (about 9.5 mA with 220 ohm at 3.3 V).
- Check the isolated 12 V gate-drive rail.
- Check the BD139/BD140 orientation and the common-base node voltage.
- Check the 10 k gate-source resistors and the gate resistors.

**MOSFETs get hot**
- Check gate drive amplitude and gate resistors.
- Check for shoot-through with a scope: side A and side B must never be high
  at the same time.
- Check the snubber/TVS and transformer leakage spike.
- Verify current sharing and heatsinking.
