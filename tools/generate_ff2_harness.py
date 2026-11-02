"""FF2の既存BGM・矩形波SEを呼ぶ検証用コードを生成する。"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Wiiz/output/deps'))
from py65.devices.mpu6502 import MPU
from py65.assembler import Assembler

assembler = Assembler(MPU())


def code(base, lines, symbols=None):
    import re
    labels = {}
    position = base
    for line in lines:
        if line.endswith(':'):
            labels[line[:-1]] = position
        else:
            position += len(assembler.assemble(re.sub(r'@\w+', '$0000', line), pc=position))
    if symbols is not None:
        symbols.update(labels)
    result = []
    for line in lines:
        if not line.endswith(':'):
            line = re.sub(r'@(\w+)', lambda m: f'${labels[m[1]]:04x}', line)
            result += assembler.assemble(line, pc=base + len(result))
    return result


boot = code(0x700, [
    'SEI', 'CLD', 'LDX #$ff', 'TXS', 'LDA #$00', 'STA $2000', 'STA $2001', 'LDX #$00',
    'clear:', 'STA $00,X', 'STA $0100,X', 'STA $0300,X', 'STA $0400,X', 'STA $0500,X', 'INX', 'BNE @clear',
    'LDA #$0d', 'JSR $fe03', 'LDX #$00', 'LDA #$00', 'prg:', 'STA $6f00,X', 'INX', 'BNE @prg',
    'JSR $98c4', 'LDA #$ff', 'STA $6f25', 'LDX #$05', 'pointer:', 'STA $b0,X', 'DEX', 'BPL @pointer',
    'LDA #$01', 'STA $04f8', 'STA $04f9', 'LDA #$40', 'STA $0100', 'LDA #$80', 'STA $2000', 'JMP $0780'])
symbols = {}
wrappers = code(0x600, [
    'ff2_request:', 'STA $e0', 'RTS',
    'ff2_beep:', 'LDA #$01', 'STA $34', 'JSR $c921', 'RTS',
    'ff2_battle:', 'PHA', 'LDA #$00', 'STA $04fc', 'LDA #$05', 'JSR $fe03', 'PLA', 'JSR $ba03', 'LDA #$0d', 'JSR $fe03', 'RTS',
    'ff2_mute:', 'STA $04fd', 'LDA #$00', 'STA $04fc', 'RTS',
    'ff2_water:', 'PHA', 'LDA #$08', 'BNE @terrain',
    'ff2_wind:', 'PHA', 'LDA #$04',
    'terrain:', 'CMP $04fc', 'BEQ @same', 'STA $04fc', 'PLA', 'STA $04fb', 'RTS',
    'same:', 'PLA', 'RTS',
    'ff2_stop:', 'PHA', 'LDA #$00', 'STA $04fc', 'PLA', 'BEQ @done', 'LDA #$03', 'STA $04fd', 'done:', 'RTS',
    'ff2_door:', 'LDA #$00', 'STA $04fc', 'JMP $c8d0',
    'ff2_post:', 'LDA $04fc', 'BEQ @mute', 'LDA $04fb', 'STA $f0', 'INC $04fb',
    'LDA $04fc', 'CMP #$08', 'BNE @wind', 'JSR $c130', 'JMP @mute', 'wind:', 'JSR $c13d',
    'mute:', 'LDA $04fd', 'BEQ @exit', 'LDX #$00', 'STX $04fd', 'CMP #$01', 'BEQ @soft', 'CMP #$02', 'BEQ @off',
    'LDA #$30', 'STA $400c', 'RTS', 'soft:', 'JMP $d913', 'off:', 'JMP $c4a0', 'exit:', 'RTS'
], symbols)
assert 0x600 + len(wrappers) <= 0x700
loop = code(0x780, [
    'JSR $fe00', 'LDX #$00', 'STX $057f',
    'next:', 'CPX $0500', 'BCS @update', 'LDA $0501,X', 'STA $07f3', 'LDA $0502,X', 'STA $07f4', 'LDA $0503,X', 'JSR $07f2',
    'LDX $057f', 'INX', 'INX', 'INX', 'STX $057f', 'JMP @next',
    'update:', 'LDA #$00', 'STA $0500', 'LDA $04f9', 'STA $04f7', 'BEQ @music',
    'LDA #$05', 'JSR $fe03', 'effects:', 'JSR $ba00', 'DEC $04f7', 'BNE @effects',
    'music:', 'LDA #$0d', 'JSR $fe03', 'LDA $04f8', 'STA $04f7', 'BEQ @post',
    'bgm:', 'JSR $9800', 'DEC $04f7', 'BNE @bgm', 'post:', f'JSR ${symbols["ff2_post"]:04x}', 'JMP $0780'])
assert 0x780 + len(loop) <= 0x7f2
nmi = code(0x7f8, ['JMP $0100'])
arrays = [('ff2_boot', boot), ('ff2_wrappers', wrappers), ('ff2_loop', loop), ('ff2_nmi', nmi)]
text = '// tools/generate_ff2_harness.pyで生成。原作ドライバーを呼ぶ接続部分のみ。\n'
for name, address in symbols.items():
    if name.startswith("ff2_"):
        text += f"#define {name.upper()}_ADDR 0x{address:04x}\n"
for name, values in arrays:
    text += 'static const uint8_t ' + name + '[]={' + ','.join(hex(v) for v in values) + '};\n'
target = Path(__file__).resolve().parents[1] / 'app/ff2_harness.h'
if '--write' in sys.argv:
    target.write_text(text, encoding='utf-8')
elif target.read_text(encoding='utf-8') != text:
    raise RuntimeError('FF2 glue differs; regenerate with --write')
from py65.disassembler import Disassembler
for base, values in [(0x700, boot), (0x780, loop)]:
    memory = bytearray(65536)
    memory[base:base + len(values)] = bytes(values)
    d = Disassembler(MPU(memory=memory))
    p = base
    while p < base + len(values):
        n, instruction = d.instruction_at(p)
        print(f'{p:04x}: {instruction}')
        p += n
