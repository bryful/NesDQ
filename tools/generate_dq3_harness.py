"""binjnesでDQ3の既存ドライバーを呼ぶ接続コードを生成する開発用ツール。"""
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
 'LDA #$80','STA $8000','LDA #$01','JSR $ffbd','LDA #$ff','STA $06f0','JSR $8581','LDA #$80','STA $2000','JMP $073d'])
nmi=code(0x780,[
 'PHA','TXA','PHA','TYA','PHA','LDX #$00','STX $057f',
 'CPX $0500','BCS $07ad','LDA $0501,X','STA $07e1','LDA $0502,X','STA $07e2','LDA $0503,X','JSR $07e0',
 'LDX $057f','INX','INX','INX','STX $057f','JMP $078a',
 'LDA #$00','STA $0500','JSR $8000','PLA','TAY','PLA','TAX','PLA','RTI'])

def listing(base,values):
 from py65.disassembler import Disassembler
 memory=bytearray(65536);memory[base:base+len(values)]=bytes(values);d=Disassembler(MPU(memory=memory));pc=base
 while pc<base+len(values):
  n,text=d.instruction_at(pc);print(f'{pc:04x}: {text}');pc+=n
listing(0x700,boot);listing(0x780,nmi)
root=Path(__file__).resolve().parents[1]
(root/'build').mkdir(exist_ok=True)
(root/'build/dq3-harness.txt').write_text('boot='+','.join(hex(v) for v in boot)+'\nnmi='+','.join(hex(v) for v in nmi))


import re
source=root/'app/core_bridge.c'
text=source.read_text(encoding='utf-8')
for name,values in [('dq3_boot',boot),('dq3_nmi',nmi)]:
 pattern=rf'static const uint8_t {name}\[\]=\{{([^}}]+)\}};'
 match=re.search(pattern,text)
 if not match:raise RuntimeError(f'{name} is missing')
 if [int(v,0) for v in match.group(1).split(',')]!=values:
  if '--write' not in sys.argv:raise RuntimeError(f'{name} differs; pass --write to regenerate')
  text=re.sub(pattern,'static const uint8_t '+name+'[]={'+','.join(hex(v) for v in values)+'};',text)
if '--write' in sys.argv:source.write_text(text,encoding='utf-8')
print('DQ3 glue matches core_bridge.c')
