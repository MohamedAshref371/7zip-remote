"""Verify selectable HTTP block sizes, exact reads, and separated disk caches."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import threading

from estimate_fixtures import write_rar4, write_rar5
from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='block-sizes-', dir=ROOT / '.build/msvc/runs'))
    target = b'selected target\n' * 20000
    entries = [('docs/first.bin', b'A' * 300000),
               ('docs/target.txt', target),
               ('docs/last.bin', b'Z' * 300000)]
    files = {}
    for name, writer in [('rar4.rar', write_rar4), ('rar5.rar', write_rar5)]:
        writer(work / name, entries)
        files[name] = (work / name).read_bytes()
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def run(name, label, size, extra=()):
        before = len(server.records)
        output = work / label
        result = subprocess.run([str(exe), f'http://127.0.0.1:{server.server_port}/ok/{name}',
                                 str(output), f'--http-block-kib={size}', *extra],
                                capture_output=True, timeout=120)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        assert result.returncode == 0, (label, log)
        assert (output / 'target.txt').read_bytes() == target, label
        return [(r['start'], r['end']) for r in server.records[before:]]

    try:
        for name, blob in files.items():
            for kib in (32, 64, 128, 256):
                ranges = run(name, f'{name}-{kib}', kib)
                assert ranges[0] == (0, 0), (name, kib, ranges)
                assert all(start % (kib << 10) == 0 and end == min(start + (kib << 10), len(blob)) - 1
                           for start, end in ranges[1:]), (name, kib, ranges)
                print(f'PASS {name} {kib} KiB: {len(ranges)} aligned requests', flush=True)
            exact32 = run(name, f'{name}-exact32', 32, ['--exact-http-reads'])
            exact256 = run(name, f'{name}-exact256', 256, ['--exact-http-reads'])
            assert exact32 == exact256 and any(end - start < 100 for start, end in exact32[1:]), name
            print(f'PASS {name} exact reads ignore block size', flush=True)
        cache = work / 'shared-cache'
        for kib in (32, 64, 128, 256):
            options = [f'--cache-dir={cache}']
            first = run('rar5.rar', f'cache-{kib}-first', kib, options)
            second = run('rar5.rar', f'cache-{kib}-reopen', kib, options)
            assert len(first) > 1 and second == [(0, 0)], (kib, first, second)
            if kib == 32:
                assert all(end - start + 1 <= (32 << 10) for start, end in first), first
            print(f'PASS {kib} KiB disk cache reopen', flush=True)
        magics = {p.read_bytes()[:8] for p in cache.glob('.7z-http-cache-v1/*/info.bin')}
        assert {b'7ZHC06I\0', b'7ZHC04I\0', b'7ZHC05I\0', b'7ZHC01I\0'} <= magics, magics
    finally:
        server.shutdown()
        server.server_close()
    print('All block size scenarios passed.')


if __name__ == '__main__':
    main()
