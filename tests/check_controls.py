"""実際のSDLイベント経路で設定保存・入力交換・速度操作を検査する。"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
exe = root / 'build/Release/nesdq.exe'
dq3, dq4, unsupported = map(lambda s: Path(s).resolve(), sys.argv[1:4])
output = Path(tempfile.mkdtemp(prefix='controls-', dir=root / 'build'))
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def tap(events, frame, key):
    events.extend([(frame, key, 'down'), (frame + 1, key, 'up')])


def run(name, rom, events, frames, swap=False, folder=None):
    folder = folder or output / name
    folder.mkdir(exist_ok=True)
    if swap:
        (folder / 'ab-swap.cfg').write_bytes(b'1')
    replay = output / (name + '.txt')
    replay.write_text(''.join(f'{f} {k} {a}\n' for f, k, a in sorted(events)))
    image = output / (name + '.bmp')
    result = subprocess.run([str(exe), '--smoke', str(rom), str(frames), str(replay), str(folder), str(image)],
                            env=env, capture_output=True, text=True)
    (output / (name + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode or 'PASS SDL replay' not in result.stdout:
        raise RuntimeError(name + ': ' + result.stdout + result.stderr)
    return folder, image.read_bytes()


events = []
tap(events, 2, 'Q')
for i in range(12):
    tap(events, 4 + 2*i, 'Down')
tap(events, 28, 'X')
folder, _ = run('toggle', dq3, events, 34)
assert (folder / 'ab-swap.cfg').read_bytes() == b'1'
run('toggle-restart', dq3, events, 34, folder=folder)
assert (folder / 'ab-swap.cfg').read_bytes() == b'0'

images = []
for name, key, swap in [('normal-a', 'X', False), ('swapped-b', 'Z', True), ('normal-b', 'Z', False)]:
    events = []
    tap(events, 1400, key)
    _, image = run(name, dq3, events, 1850, swap)
    images.append(image)
assert images[0] == images[1], 'Swapped B must produce the same game image as normal A'
assert images[0] != images[2], 'The selected game scene must distinguish A and B'

events = []
tap(events, 2, 'Q')
for i in range(11):
    tap(events, 4 + 2*i, 'Down')
tap(events, 26, 'Left')
tap(events, 28, 'Right')
tap(events, 30, 'Right')
tap(events, 32, 'X')
folder, _ = run('speed', dq4, events, 38)
identity = hashlib.sha256(dq4.read_bytes()).hexdigest()
assert (folder / (identity + '.speed')).read_bytes() == b'2'
folder, _ = run('unsupported-speed', unsupported, events, 38)
assert not list(folder.glob('*.speed'))
print('PASS controls: A/B mapping, persistence, menu, speed wrap, unsupported ROM')
print(output)
