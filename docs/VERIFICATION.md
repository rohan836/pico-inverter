# Verification report

Prior timing verification of the PIO generator; see REVIEW.md for subsequent changes.
UF2 SHA-256: B7585E3575BF36ACCA6D827B021F259456D21B44EA35A83F45BC2DA5386666BA

| Check | Result |
|---|---|
| Default firmware compiles and links | PASS |
| SSD1306, soft-start timer, exact-50 bench variants compile and link | PASS |
| Build logs scanned for compiler warning/error messages | PASS: none found |
| Production C timing: every combination of 1–60 Hz and 1–100% requested duty | PASS: 6,000 combinations |
| Assembled PIO instructions: alternating channels, no overlap, equal pulses and gaps over two cycles | PASS: 6,000 combinations |
| Minimum 200 us gaps and maximum 10 ms ON time | PASS in host verification |
| ELF converted to UF2 | PASS |
| UF2 copied to Pico BOOTSEL volume | PASS |
| Pico returns as COM6 | PASS |
| Actual GPIO edges, voltage, jitter and scope agreement | NOT MEASURED |
| Output power and electrical efficiency | NOT MEASURED |

## Expected steady-state timings

| Frequency setting | Duty setting | Period | ON per channel | Gap per transition | Actual duty |
|---|---|---|---|---|---|
| 50 Hz | 25% | 20.000 ms | 5.000 ms | 5.000 ms | 25% |
| 50 Hz | 50–100% | 20.000 ms | 9.800 ms | 0.200 ms | 49% |
| 60 Hz | 25% | 16.666 ms | 4.166 ms | 4.167 ms | 24.997% |
| 60 Hz | 50–100% | 16.666 ms | 8.133 ms | 0.200 ms | 48.800% |
| 1 Hz | 100% | 1000.000 ms | 10.000 ms | 490.000 ms | 1% |

The 60 Hz setting achieves approximately 60.0024 Hz before clock tolerance.
Duty values on the dashboard are requested values; LIM indicates clamping.
The old photo's 49.4% at 60 Hz is not the expected maximum of this firmware.

## Changes and limits

The previous phase-correct PWM implementation could overflow its 16-bit counter
at low frequencies after exhausting its divider range. It has been replaced
with a PIO state machine using 32-bit loop counts and a 1 MHz instruction clock.
The new verification runs the production timing C and decodes the assembled PIO
instructions, rather than relying on the older independent timing model.

Steady-state pulse edges are generated without CPU interrupts. Parameter changes now stop the sequence and require explicit PWM ON. PWM OFF
immediately stops the machine and switches both GPIOs low. Startup and shutdown
were reviewed in source but have not been measured electrically.

Sharper electrical corners, rail droop, noise, losses, transformer power rating,
and efficiency are not established by these tests. No power or efficiency gain
is claimed. The dead-time and pulse limits have not been relaxed for more power.
