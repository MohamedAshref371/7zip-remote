"""Check exact HTTP ranges, cache overlaps, extraction and download savings."""
import argparse
import json
from pathlib import Path
import random
import subprocess
import tempfile
import threading
import zipfile

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='exact-reads-', dir=ROOT / '.build/msvc/runs'))
    inputs = work / 'input'
    (inputs / 'docs').mkdir(parents=True)
    target = b'selected remote member\n' * 100
    (inputs / 'docs/target.txt').write_bytes(target)
    (inputs / '000-first.bin').write_bytes(random.Random(63).randbytes(2 << 20))
    (inputs / '999-last.bin').write_bytes(random.Random(64).randbytes(2 << 20))
    with zipfile.ZipFile(work / 'sample.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        z.comment = b'x' * 65535
        for path in sorted(inputs.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(inputs).as_posix())
    for name, flags in [('sample.7z', ['-ms=off']),
                        ('solid.7z', ['-ms=on']),
                        ('secret.7z', ['-ms=off', '-mhe=on', '-pfixture-password'])]:
        subprocess.run([str(exe.with_name('7z.exe')), 'a', str(work / name), *flags, '.'],
                       cwd=inputs, capture_output=True, check=True)
    files = {p.name: p.read_bytes() for p in work.iterdir() if p.suffix in ('.zip', '.7z')}
    files['raw.bin'] = bytes(range(256)) * 4096
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, name, options=(), mode='ok', success=True, output=None):
        start = len(server.records)
        output = output or work / label
        result = subprocess.run([str(exe), f'{base}/{mode}/{name}', str(output), *options],
                                capture_output=True, timeout=120)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(log, encoding='utf-8')
        assert (result.returncode == 0) == success, (label, result.returncode, log)
        records = server.records[start:]
        total = sum(r['end'] - r['start'] + 1 for r in records)
        cases.append(dict(label=label, bytes=total, requests=len(records)))
        if success and name != 'raw.bin':
            assert (output / 'target.txt').read_bytes() == target, label
        print(f'PASS {label}: {total} bytes, {len(records)} requests', flush=True)
        return records, total

    try:
        records, _ = run('stream', 'raw.bin', ['--exact-stream-check'])
        assert [(r['start'], r['end']) for r in records] == [
            (0, 0), (100, 102), (98, 99), (103, 105), (262143, 262145),
            (500000, 762143), (1048574, 1048575)]
        assert not (work / 'stream').exists(), 'exact mode must not create disk cache files'
        raw_cache = work / 'raw-cache'
        _, total = run('disk-stream-first', 'raw.bin', ['--exact-disk-stream-check'], output=raw_cache)
        assert total == 262158
        parts = list(raw_cache.glob('.7z-http-cache-v1/*/b*.bin'))
        assert sorted(p.stat().st_size - 88 for p in parts) == [2, 2, 3, 3, 3, 262144]
        # Persisted payload bytes are precisely the requested data, with no padding.
        assert sum(p.stat().st_size - 88 for p in parts) == total - 1
        records, total = run('disk-stream-reopen', 'raw.bin', ['--exact-disk-stream-check'], output=raw_cache)
        assert total == 1 and len(records) == 1
        _, total = run('disk-stream-slices', 'raw.bin', ['--exact-disk-slice-check'], output=raw_cache)
        assert total == 1
        broken = next(p for p in parts if p.name == 'b0000000000000064.bin')
        damaged = bytearray(broken.read_bytes())
        damaged[-1] ^= 1
        broken.write_bytes(damaged)
        records, total = run('disk-stream-repair', 'raw.bin', ['--exact-disk-stream-check'], output=raw_cache)
        assert total == 4 and [(r['start'], r['end']) for r in records] == [(0, 0), (100, 102)]
        for name in ('sample.zip', 'sample.7z', 'solid.7z', 'secret.7z'):
            _, baseline = run(name + '-blocks', name)
            _, exact = run(name + '-exact', name, ['--exact-http-reads', '--selection-check'])
            if name in ('sample.zip', 'sample.7z', 'secret.7z'):
                assert exact < baseline, (name, exact, baseline)
            cache = work / (name + '-cache')
            disk_options = ['--exact-http-reads', '--selection-check', '--cache-dir=' + str(cache)]
            _, cached = run(name + '-disk-first', name, disk_options)
            assert cached <= exact
            records, reopened = run(name + '-disk-reopen', name, disk_options)
            assert reopened == 1 and len(records) == 1
            run(name + '-disk-quota', name, [*disk_options, '--cache-limit=1024'])
        run('bypass', 'sample.zip', ['--exact-http-reads', '--skip-http-etag'], mode='missing-etag')
        for mode in ('missing-etag', 'changed-size', 'wrong-range', 'truncated'):
            run('reject-' + mode, 'sample.zip', ['--exact-http-reads'], mode=mode, success=False)
        run('renew', 'sample.zip', ['--exact-http-reads', '--renew-url=' + base + '/ok/sample.zip?new=1'])
    finally:
        server.shutdown()
        server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases, requests=server.records), indent=2), encoding='utf-8')
    print(f'All {len(cases)} exact-read scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
