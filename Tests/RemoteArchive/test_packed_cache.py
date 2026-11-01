"""Stress compact parts, legacy cache reads, integrity and safe clearing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import threading

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / '.build/remote/cachecheck.exe')
    parser.add_argument('--legacy-agent', type=Path, default=ROOT / '.build/msvc/exact-disk-cache/agentcheck.exe')
    parser.add_argument('--skip-legacy', action='store_true', help='Run compact-cache checks without a previous-version binary')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='packed-cache-', dir=ROOT / '.build/msvc/runs'))
    data = bytes(range(256)) * 32768
    server = FixtureServer({'raw.bin': data})
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/ok/raw.bin'
    cases = []

    def run(label, folder, count=1000, size=3, stride=4096, first=0, extra=()):
        result = subprocess.run([str(args.exe.resolve()), url, str(folder), '--exact-http-reads',
            f'--fragment-size={size}', f'--fragment-stride={stride}', f'--cache-start={first}',
            f'--cache-blocks={count}', *extra], capture_output=True, timeout=120)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(log, encoding='utf-8')
        assert result.returncode == 0, (label, log)
        match = re.search(r'DISK \w+ (\d+) (\d+) (\d+)', log)
        if match:
            expected = b''.join(server.files['raw.bin'][(i + first) * stride:(i + first) * stride + size]
                                for i in range(count))
            assert hashlib.sha256(expected).hexdigest() in log, (label, log)
        stats = tuple(map(int, match.groups())) if match else ()
        cases.append(dict(label=label, stats=stats))
        print('PASS', label, stats, flush=True)
        return stats

    try:
        cache = work / 'compact'
        assert run('thousand-tiny-parts', cache) == (1001, 3001, 0)
        packs = list(cache.glob('.7z-http-cache-v1/*/parts.bin'))
        assert len(packs) == 1 and packs[0].stat().st_size == 1000 * (120 + 3)
        assert not list(cache.glob('.7z-http-cache-v1/*/b*.bin'))
        assert run('thousand-parts-reopen', cache) == (1, 1, 1000)
        # Different request boundaries inside the saved ranges need no new body.
        assert run('interior-range', cache, count=1, size=1, stride=4097, first=1) == (1, 1, 1)
        run('quota-eviction', cache, extra=('--cache-limit=4096',))
        assert sum(p.stat().st_size for p in cache.rglob('*') if p.is_file()) <= 4096
        run('clear-compact', cache, extra=('--cache-clear',))
        assert not list(cache.glob('.7z-http-cache-v1/*/parts.bin'))
        if not args.skip_legacy:
            legacy = work / 'legacy'
            result = subprocess.run([str(args.legacy_agent.resolve()), url, str(legacy), '--exact-disk-stream-check'],
                                    capture_output=True, timeout=60)
            assert result.returncode == 0, result.stdout + result.stderr
            assert run('legacy-v2-reuse', legacy, count=1, first=1, stride=100) == (1, 1, 1)
            old = next(legacy.glob('.7z-http-cache-v1/*/b0000000000000064.bin'))
            broken = bytearray(old.read_bytes()); broken[-1] ^= 1; old.write_bytes(broken)
            assert run('legacy-repair-to-pack', legacy, count=1, first=1, stride=100) == (2, 4, 0)
            assert run('mixed-legacy-and-pack-reopen', legacy, count=1, first=1, stride=100) == (1, 1, 1)
            server.etag = '"fixture-v2"'
            changed = bytearray(data); changed[100] ^= 1; server.files['raw.bin'] = bytes(changed)
            assert run('changed-version-isolated', legacy, count=1, first=1, stride=100) == (2, 4, 0)
            run('clear-both-generations', legacy, extra=('--cache-clear',))
            assert not list(legacy.glob('.7z-http-cache-v1/*/*.bin'))
    finally:
        server.shutdown(); server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} compact-cache scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
