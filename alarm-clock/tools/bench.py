#!/usr/bin/env python3
"""USB bench client. Run with ~/.platformio/penv/bin/python (pyserial).
Commands use normal UI events; frame captures the actual OLED framebuffer.
Example: tools/bench.py status select frame:menu cw select frame:alarms
Opening the CH343 port may reset this board; each invocation starts at Home.
"""
import argparse
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='/dev/cu.usbmodem5C930434691')
parser.add_argument('--output', type=Path, default=Path('artifacts'))
parser.add_argument('commands', nargs='*', default=['status'])
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
log = []
port = serial.Serial(args.port, 115200, timeout=.05)
port.dtr = False
port.rts = False
pending = b''

def receive(seconds, frame=None):
    global pending
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        pending += port.read(8192)
        while b'\n' in pending:
            raw, pending = pending.split(b'\n', 1)
            line = raw.decode(errors='replace').strip()
            log.append(line)
            if line.startswith('[frame] '):
                data = bytes.fromhex(line[8:])
                if len(data) != 1024:
                    raise RuntimeError('Incomplete framebuffer')
                if frame:
                    # U8g2 stores vertical groups of eight pixels per byte.
                    rows = [' '.join('1' if data[x + (y//8)*128] & (1 << (y%8)) else '0'
                                     for x in range(128)) for y in range(64)]
                    dest = args.output / f'{frame}.pbm'
                    dest.write_text('P1\n128 64\n' + '\n'.join(rows) + '\n')
                    print(f'[capture] {dest}')
            else:
                print(line)

try:
    receive(2.5)
    for cmd in args.commands:
        if cmd.startswith('wait:'):
            receive(float(cmd.split(':', 1)[1]))
            continue
        parts = cmd.split(':', 2) if cmd.startswith('frame:') else []
        frame = parts[1] if parts else None
        wire_cmd = (parts[2] if len(parts) == 3 else 'frame') if parts else cmd
        port.write((wire_cmd + '\n').encode())
        receive(.3 if not frame else .5, frame)
finally:
    port.close()
    (args.output / 'last-serial.log').write_text('\n'.join(log) + '\n')
