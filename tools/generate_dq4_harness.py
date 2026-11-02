"""binjnesでDQ4の既存ドライバーを呼ぶ接続コードを生成する開発用ツール。"""
import sys
import re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'Wiiz/output/deps'))
from py65.devices.mpu6502 import MPU
from py65.assembler import Assembler
assembler=Assembler(MPU())
def code(base,lines):
    result=[]
    for line in lines:
        result+=assembler.assemble(line,pc=base+len(result))
    return result
boot=code(0x700,[
 'SEI','CLD','LDX #$ff','TXS','LDA #$00','STA $2000','STA $2001','LDX #$00',
 'STA $00,X','STA $0100,X','STA $0200,X','STA $0300,X','STA $0400,X','STA $0500,X','STA $0600,X','INX','BNE $070f',
 'LDA #$0e','JSR $c118','LDA #$10','JSR $c12f','LDA #$19','STA $6e86','JSR $ff91','JSR $f088',
 'LDA #$80','STA $2000','JMP $0740'])
# 分岐先は生成結果から確認する。音声更新の呼出し以外は待機する。

nmi=code(0x780,[
 'PHA','TXA','PHA','TYA','PHA','LDX #$00','STX $061f',
 'CPX $0600','BCS $07ad','LDA $0680,X','STA $07e1','LDA $0681,X','STA $07e2','LDA $0682,X','JSR $07e0',
 'LDX $061f','INX','INX','INX','STX $061f','JMP $078a',
 'LDA #$00','STA $0600','JSR $e685','PLA','TAY','PLA','TAX','PLA','RTI'])
def listing(base,values):
 from py65.disassembler import Disassembler
 memory=bytearray(65536);memory[base:base+len(values)]=bytes(values);d=Disassembler(MPU(memory=memory));pc=base
 while pc<base+len(values):
  n,text=d.instruction_at(pc);print(f'{pc:04x}: {text}');pc+=n
listing(0x700,boot);listing(0x780,nmi)
root=Path(__file__).resolve().parents[1]
(root/'build').mkdir(exist_ok=True)
(root/'build/dq4-harness.txt').write_text('boot='+','.join(hex(v) for v in boot)+'\nnmi='+','.join(hex(v) for v in nmi))

source=root/'app/core_bridge.c'
text=source.read_text(encoding='utf-8')
for name,values in [('boot',boot),('nmi',nmi)]:
    pattern=rf'static const uint8_t {name}\[\]=\{{([^}}]+)\}};'
    match=re.search(pattern,text)
    if not match:raise RuntimeError(f'{name} is missing')
    actual=[int(v,0) for v in match.group(1).split(',')]
    if actual!=values:
        if '--write' not in sys.argv:raise RuntimeError(f'{name} differs; pass --write to regenerate')
        text=re.sub(pattern,'static const uint8_t '+name+'[]={'+','.join(hex(v) for v in values)+'};',text)
if '--write' in sys.argv:source.write_text(text,encoding='utf-8')
print('glue matches core_bridge.c')
