"""FF3の原作音声ドライバーを音声専用コアから呼ぶ接続コード。"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Wiiz/output/deps'))
from py65.devices.mpu6502 import MPU
from py65.assembler import Assembler
import re
assembler = Assembler(MPU())
def code(base, lines):
    labels = {}; position = base
    for line in lines:
        if line.endswith(':'): labels[line[:-1]] = position
        else: position += len(assembler.assemble(re.sub(r'@\w+', '$0000', line), pc=position))
    result = []
    for line in lines:
        if not line.endswith(':'):
            line = re.sub(r'@(\w+)', lambda m: f'${labels[m[1]]:04x}', line)
            result += assembler.assemble(line, pc=base+len(result))
    return result
boot = code(0x700, [
    'SEI','CLD','LDX #$ff','TXS','LDA #$00','STA $2000','STA $2001','LDX #$00',
    'clear:','STA $00,X','STA $0100,X','STA $0300,X','STA $0400,X','STA $0500,X','INX','BNE @clear',
    'LDA #$00','STA $a000','STA $e000','LDA #$1b','JSR $fb87','JSR $8000',
    'LDA #$37','STA $7f43','LDA #$01','STA $04f8','LDA #$80','STA $2000','JMP $0780'])
wrappers = code(0x600, ['STA $7f40','RTS','STA $7f42','RTS','STA $7f43','RTS','STA $7f49','RTS'])
loop = code(0x780, [
    'wait:','LDA $04fe','BEQ @wait','LDA #$00','STA $04fe','LDX #$00','STX $057f',
    'next:','CPX $0500','BCS @update','LDA $0501,X','STA $07e1','LDA $0502,X','STA $07e2','LDA $0503,X','JSR $07e0',
    'LDX $057f','INX','INX','INX','STX $057f','JMP @next',
    'update:','LDA #$00','STA $0500','LDA $04f8','BEQ @done','STA $04f7',
    'again:','JSR $8003','DEC $04f7','BNE @again','done:','JMP $0780'])
nmi = code(0x7f0, ['INC $04fe','RTI'])
assert len(boot) <= 0x80 and 0x780+len(loop) <= 0x7e0
text = '// tools/generate_ff3_harness.pyで生成。原作ドライバーを呼ぶ接続部分のみ。\n'
for name, values in [('ff3_boot',boot),('ff3_wrappers',wrappers),('ff3_loop',loop),('ff3_nmi',nmi)]:
    text += 'static const uint8_t '+name+'[]={'+','.join(hex(v) for v in values)+'};\n'
target = Path(__file__).resolve().parents[1] / 'app/ff3_harness.h'
if '--write' in sys.argv: target.write_text(text, encoding='utf-8')
elif target.read_text(encoding='utf-8') != text: raise RuntimeError('FF3 glue differs; regenerate with --write')
