"""Compare the compiled production C++ with pinned edge-tts 7.3.0."""
import random
import subprocess
import sys
from pathlib import Path
root = Path(__file__).resolve().parent
source = (root / 'reference/drm.py').read_text()
source = source[:source.index('class DRM:')]
source = '\n'.join(line for line in source.splitlines() if not line.startswith(('import aiohttp','from .')))
scope = {'TRUSTED_CLIENT_TOKEN': '6A5AA1D4EAFF4E9FB37E23D68491D6F4'}
exec(compile(source, 'edge-tts-7.3.0/drm.py', 'exec'), scope)
random.seed(730)
timestamps = [0, 1, 299, 300, 301, 1700000000, 1791964800, 2147483647]
timestamps += [random.randrange(0, 4000000000) for _ in range(300)]
ticks = [(t + 11644473600)*10000000 for t in timestamps]
# Exact Windows tick boundaries and the last tick before the next window.
ticks += [ticks[6]//3000000000*3000000000 + delta for delta in (-1,0,1,2999999999,3000000000)]
ticks += [0, 1, 2999999999, 3000000000, 18446744073709551615]
actual = subprocess.check_output([str(root/'gec_cli.exe'), *map(str,ticks)], text=True).splitlines()
for tick, value in zip(ticks, actual, strict=True):
    # Compare the exact rounded FILETIME string, without Python float rounding.
    data = f'{tick//3000000000*3000000000}{scope["TRUSTED_CLIENT_TOKEN"]}'.encode('ascii')
    cs64, digest = scope['_token_hash'](data)
    expected = f'{cs64}-{digest}'
    assert value == expected, (tick,value,expected)
print(f'PASS: {len(ticks)} v2 tokens match edge-tts 7.3.0, including five-minute boundaries')
