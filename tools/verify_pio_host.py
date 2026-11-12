"""Verify production timing C and assembled PIO instructions over 6000 settings.
Run after the firmware build: python tools/verify_pio_host.py
Requires host gcc (or set HOST_CC), not the ARM cross compiler.
"""
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / 'build'
exe = build / 'timing_host.exe'
subprocess.run([os.environ.get('HOST_CC', 'gcc'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-I' + str(root / 'src'), str(root / 'tests/timing_host.c'),
                '-o', str(exe)], check=True)
header = (build / 'inverter.pio.h').read_text()
program = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{4}),', header)]
wrap = int(re.search(r'#define inverter_wrap (\d+)', header)[1])
target = int(re.search(r'#define inverter_wrap_target (\d+)', header)[1])


def edges(on_count, off_count):
    fifo = iter([on_count, off_count])
    pc = clock = x = isr = osr = 0
    result = []
    while len(result) < 9:
        instruction = program[pc]
        op, arg = instruction >> 13, instruction & 255
        assert instruction & 0x1f00 == 0, 'Unexpected side-set or delay'
        next_pc = target if pc == wrap else pc + 1
        elapsed = 1
        if op == 4:  # PULL
            assert instruction == 0x80a0
            osr = next(fifo)
        elif op == 5:  # MOV
            dest, source = (arg >> 5) & 7, arg & 7
            value = {6: isr, 7: osr}[source]
            if dest == 1:
                x = value
            elif dest == 6:
                isr = value
            else:
                raise AssertionError('Unexpected MOV')
        elif op == 7:  # SET pins
            assert arg >> 5 == 0
            value = arg & 31
            assert value in (0, 1, 2), 'Output overlap'
            result.append((clock, value))
        elif op == 0:  # JMP X-- to itself: count all iterations in one step
            assert (arg >> 5) == 2 and arg & 31 == pc
            elapsed = x + 1
            x = 0xffffffff
        else:
            raise AssertionError('Unexpected instruction')
        clock += elapsed
        pc = next_pc
    return result

rows = subprocess.check_output([str(exe)], text=True).splitlines()
assert len(rows) == 6000
for row in rows:
    hz, duty, on, off, period, on_count, off_count, limited = map(int, row.split())
    assert period == 2 * (1000000 // (2 * hz))
    expected = min(period * duty // 100, period // 2 - 200, 10000)
    assert on == expected and on + off == period // 2
    assert off >= 200 and 0 < on <= 10000
    assert limited == (on < period * duty // 100)
    trace = edges(on_count, off_count)
    assert [v for _, v in trace] == [1, 0, 2, 0, 1, 0, 2, 0, 1]
    for i in range(8):
        assert trace[i+1][0] - trace[i][0] == (on if i % 2 == 0 else off)
    assert trace[4][0] - trace[0][0] == period
    assert trace[8][0] - trace[4][0] == period
print('PASS: 6000 production timing settings and assembled PIO pulse sequences')
for hz, duty in [(50, 25), (50, 50), (60, 25), (60, 50), (1, 100)]:
    row = rows[(hz - 1) * 100 + duty - 1].split()
    on, off, period = map(int, row[2:5])
    print(f'{hz} Hz / {duty}%: ON={on} us, gap={off} us, period={period} us, actual duty={100*on/period:.4f}%')
