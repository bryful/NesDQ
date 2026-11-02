"""別の6502実装で、原作と接続コードのAPU書込を全位相で比較する。"""
import hashlib
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root.parent / 'Wiiz/output/deps'))
from py65.devices.mpu6502 import MPU

rom = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(rom).hexdigest() == '0846bf08ee66d84f72f9bcd7fde55de1c080cdd99e8d422b7a655e79e15a86ea'
header = (root / 'app/ff2_harness.h').read_text(encoding='utf-8')
addresses = {name.lower(): int(address, 16) for name, address in re.findall(r'#define (FF2_\w+)_ADDR (0x\w+)', header)}
code = bytes(int(value, 0) for value in re.search(r'ff2_wrappers\[\]=\{([^}]+)', header)[1].split(','))


class Memory(list):
    def __init__(self, patched):
        super().__init__([0] * 65536)
        self.writes = []
        self[0x8000:0xc000] = rom[16 + 13 * 16384:16 + 14 * 16384]
        self[0xc000:] = rom[16 + 15 * 16384:]
        if patched:
            self[0x600:0x600 + len(code)] = code
            self[0xc156] = self[0xc8df] = 0x60

    def __setitem__(self, key, value):
        if isinstance(key, int) and 0x4000 <= key <= 0x4017:
            self.writes.append((key, value))
        super().__setitem__(key, value)


def call(memory, target, value=0, stop=0x7ff):
    cpu = MPU(memory=memory)
    cpu.pc, cpu.a, cpu.sp = target, value, 0xfd
    memory[0x1fe], memory[0x1ff] = 0xfe, 7
    for _ in range(500):
        if cpu.pc == stop:
            return
        cpu.step()
    raise AssertionError(f'Subroutine did not return: {target:04x} -> {cpu.pc:04x}')


for mode, target in [(8, 0xc130), (4, 0xc13d)]:
    for phase in range(256):
        reference, actual = Memory(False), Memory(True)
        reference[0xf0] = phase
        call(reference, target, stop=0xc156)
        actual[0x4fc], actual[0x4fb] = mode, phase
        call(actual, addresses['ff2_post'])
        assert actual.writes == reference.writes, (mode, phase, actual.writes, reference.writes)
        assert actual[0x4fb] == (phase + 1) % 256
print('PASS terrain: original APU writes matched, 2 modes x 256 phases')

for command, target in [(1, 0xd913), (2, 0xc4a0)]:
    reference, actual = Memory(False), Memory(True)
    call(reference, target)
    actual[0x4fc] = 8
    call(actual, addresses['ff2_mute'], command)
    call(actual, addresses['ff2_post'])
    assert actual[0x4fc] == actual[0x4fd] == 0
    assert actual.writes == reference.writes
print('PASS mute: original APU writes matched, ambient stopped')

reference, actual = Memory(False), Memory(True)
call(reference, 0xc8d0, stop=0xc8df)
actual[0x4fc] = 4
call(actual, addresses['ff2_door'])
assert actual[0x4fc] == 0 and actual.writes == reference.writes
print('PASS door: original APU writes matched, ambient stopped')
