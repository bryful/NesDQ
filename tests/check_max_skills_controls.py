"""勝利後熟練度スイッチのSDL操作・永続化・対象制限を検査する。"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
output=Path(tempfile.mkdtemp(prefix='max-skills-controls-',dir=root/'build'))
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')

def run(rom,folder,name,actions):
    events=[(2,'Q')]+[(4+2*i,'Down') for i in range(16)]
    events += [(38+2*i,key) for i,key in enumerate(actions)]
    replay=output/'replay.txt'
    replay.write_text(''.join(f'{f} {k} down\n{f+1} {k} up\n' for f,k in events))
    result=subprocess.run([str(root/'build/Release/nesdq.exe'),'--smoke',str(rom),str(44+2*len(actions)),str(replay),str(folder),str(output/(name+'.bmp'))],env=env,capture_output=True,text=True)
    (output/(name+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0 and 'PASS SDL replay' in result.stdout,result.stderr

for index,arg in enumerate(sys.argv[1:]):
    rom=Path(arg).resolve();folder=output/str(index);folder.mkdir()
    config=folder/(hashlib.sha256(rom.read_bytes()).hexdigest()+'.max-skills')
    run(rom,folder,f'{index}-default',[])
    assert not config.exists()
    if index>=2:
        run(rom,folder,f'{index}-unsupported',['Right','X','Left'])
        assert not config.exists()
        continue
    for name,key,value in [('right','Right',b'1'),('left','Left',b'0'),('enter','X',b'1')]:
        run(rom,folder,f'{index}-{name}',[key]);assert config.read_bytes()==value
    run(rom,folder,f'{index}-restart',[]);assert config.read_bytes()==b'1'
    run(rom,folder,f'{index}-reset',['Down','Down','X','Q']+['Down']*16)
    assert config.read_bytes()==b'1'
print('PASS max skills controls: default off, left/right/enter, restart, reset, DQ exclusion')
print(output)
