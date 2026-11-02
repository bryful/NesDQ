"""報酬設定の実SDLメニュー経路とROMごとの永続化を検査する。"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
output = Path(tempfile.mkdtemp(prefix='reward-controls-', dir=root / 'build'))
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
exe = root / 'build/Release/nesdq.exe'

def run(rom, folder, actions, screenshot='menu'):
    events = [(2, 'Q')]
    events += [(4 + i * 2, 'Down') for i in range(14)]
    events += [(34 + i * 2, key) for i, key in enumerate(actions)]
    replay = output / 'replay.txt'
    replay.write_text(''.join(f'{frame} {key} down\n{frame+1} {key} up\n' for frame, key in events))
    result = subprocess.run([str(exe), '--smoke', str(rom), str(40 + 2 * len(actions)), str(replay), str(folder), str(output / (screenshot + '.bmp'))], env=env, capture_output=True, text=True)
    (output / (screenshot + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0 and 'PASS SDL replay' in result.stdout, result.stderr

for index, name in enumerate(sys.argv[1:]):
    rom = Path(name).resolve()
    folder = output / str(index)
    folder.mkdir()
    setting = folder / (hashlib.sha256(rom.read_bytes()).hexdigest() + '.rewards')
    run(rom, folder, [], f'{index}-default')
    assert not setting.exists()
    if index == 3:  # FF2は経験値だけ無効。
        run(rom, folder, ['Right'], 'ff2-exp-disabled')
        assert not setting.exists()
        for factor in (4, 8, 16, 64, 128, 1):
            run(rom, folder, ['Down', 'Right'], f'ff2-gold-{factor}')
            assert setting.read_bytes() == bytes([1, factor])
        continue
    for factor in (4, 8, 16, 64, 128, 1):
        run(rom, folder, ['Right'], f'{index}-exp-{factor}')
        assert setting.read_bytes() == bytes([factor, 1])
    run(rom, folder, ['Left', 'Down', 'Right'], f'{index}-separate')
    assert setting.read_bytes() == bytes([128, 4])
    run(rom, folder, ['X', 'Down', 'X'], f'{index}-enter')
    assert setting.read_bytes() == bytes([128, 4])
print('PASS reward controls: all factors, wrap, separate settings, restart, FF2 gold only')
print(output)
