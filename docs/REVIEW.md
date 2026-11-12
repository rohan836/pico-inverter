# Project review

## Implemented fixes

- Default PIO frequency/duty changes stop output and require explicit PWM ON.
  The UI applies this stop behavior to all waveform variants. Repeated remote
  adjustments no longer automatically interrupt and restart the same phase.
  An OFF command can still truncate a pulse; this is deliberate immediate shutdown.
- Drive pins initialize low before USB/display initialization.
- Configuration saving refuses to run while output is active.
- CRC-valid configurations with frequency/duty outside the supported ranges or
  an invalid configured flag are rejected. Configuration size is checked against
  the flash page at compile time.
- Deferred settings saves retry on failure instead of silently clearing pending state.
- Fixed the misleading gate/drain/source ASCII diagram and gate-source zener
  branch. Documented the required driver-ground-to-MOSFET-source reference.
- Removed unsupported claims that a 10 ms limit or the discrete driver guarantees
  transformer/MOSFET protection.

## Verified

- Default firmware build and ELF-to-UF2 conversion passed.
- 6,000 production C timing settings and assembled PIO instruction traces passed.
- Host SDK-shim tests of the production PIO driver passed: startup OFF, enable,
  disable, loaded timing counts, and changed-settings remaining OFF.
- Default firmware copied to Pico BOOTSEL; device reconnected on COM6.
- Host tests do not measure the PCB, gate drive, current or temperature.

## Timing at 60 Hz

Period 16.666 ms (nominal 60.0024 Hz before clock tolerance).
At requested 25%: each ON pulse is 4.166 ms and each gap is 4.167 ms.
At requested 50–100%: each ON pulse is 8.133 ms and each gap is 0.200 ms,
for an actual per-channel duty of 48.8%. LIM indicates the clamp.
The scope's negative duty is the low time of that channel, not the other channel.

## Unresolved hardware requirements

No maximum output power or efficiency has been established. No guarantee against
MOSFET heating/damage can be made from GPIO traces or host tests.

The documented IRF540 is specified for on-resistance at 10 V gate drive, not
3.3 V. Five parallel devices multiply the gate charge the driver must move.
The discrete PC123/BD139/BD140 circuit is not validated for that load, especially
turn-off through its resistor network. The 200 us GPIO gap must be checked
against actual gate-to-source turn-off under load and temperature.

Recommended redesign direction: a suitable gate driver with strong source/sink
current, undervoltage lockout and a disable input; independent current-trip and
thermal shutdown must force both banks off. Component choice and trip values
require battery range, transformer data, target power and measured switching
behavior. Existing snubber/TVS examples are not verified component selections.

References:
- https://www.vishay.com/docs/91021/irf540.pdf
- https://www.ti.com/product-category/power-management/gate-drivers/low-side/overview.html

## Remaining software limits

The old timer and zero-dead-time bench modes are not validated for power-stage
operation. Flash sequence rollover and power-loss fault injection have not been
tested. Setup-wizard save-error reporting still needs improvement. There is no
current or temperature sensor input and no hardware fault-disable line in the
existing pin map. The watchdog is not a substitute for fast hardware protection.
