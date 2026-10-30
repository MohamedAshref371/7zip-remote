"""Persisted cache through actual File Manager open/browse/extract/test flows."""
import argparse
import json
import hashlib
from pathlib import Path
import random
import re
import subprocess
import tempfile
import threading
import zipfile

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--checker', type=Path, default=ROOT / '.build/remote/cachecheck.exe')
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='cache-agent-', dir=ROOT / '.build/msvc/runs'))
    inputs = work / 'input'; (inputs / 'docs').mkdir(parents=True)
    rng = random.Random(91)
    target = rng.randbytes(700000)
    (inputs / 'docs/target.txt').write_bytes(target)
    for name in ('000-first.bin', '999-last.bin'): (inputs / name).write_bytes(rng.randbytes(2 << 20))
    with zipfile.ZipFile(work / 'sample.zip', 'w', zipfile.ZIP_STORED) as archive:
        for path in sorted(inputs.rglob('*')):
            if path.is_file(): archive.write(path, path.relative_to(inputs).as_posix())
    for name, solid in [('nonsolid.7z', 'off'), ('solid.7z', 'on')]:
        subprocess.run([str(exe.with_name('7z.exe')), 'a', str(work / name), '-t7z', '-ms=' + solid, '.'],
                       cwd=inputs, capture_output=True, check=True)
    files = {name: (work / name).read_bytes() for name in ('sample.zip', 'nonsolid.7z', 'solid.7z')}
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, name, mode='ok', options=()):
        out = work / label
        cache = work / ('cache-' + name + ('-weak' if mode == 'missing-etag' else ''))
        command = [str(exe), f'{base}/{mode}/{name}?sig=cache-agent-secret', str(out),
                   '--cache-dir=' + str(cache), '--selection-check', *options]
        result = subprocess.run(command, capture_output=True, timeout=30)
        text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(text, encoding='utf-8')
        assert result.returncode == 0, (label, result.returncode, text)
        assert 'cache-agent-secret' not in text
        assert (out / 'target.txt').read_bytes() == target
        progress = re.findall(r'NETWORK (open|extract|test) (\d+) (\d+) (\d+)', text)
        assert [p[0] for p in progress] == ['open', 'extract', 'test'], text
        hits = re.search(r'ARCHIVE_CACHE (\d+) (.+)', text)
        assert hits, text
        after = re.search(r'PREVIEW after (\d+) (\d+) (\d+)', text)
        assert after and int(after[2]) == len(target) and after[3] == '0', text
        stats = dict(label=label, downloaded=sum(int(p[1]) for p in progress), hits=int(hits[1]))
        cases.append(stats); print('PASS ' + label, flush=True)
        return stats, text

    try:
        for name in files:
            first, _ = run(name + '-first', name)
            second, text = run(name + '-reopen', name)
            assert first['downloaded'] > 700000 and first['hits'] == 0
            assert second['downloaded'] == 1 and second['hits'] > 0 and 'Cache verified' in text
        first, _ = run('weak-first', 'sample.zip', 'missing-etag', ('--skip-http-etag',))
        second, _ = run('weak-without-consent', 'sample.zip', 'missing-etag', ('--skip-http-etag',))
        assert first['hits'] == second['hits'] == 0
        third, text = run('weak-with-consent', 'sample.zip', 'missing-etag', ('--skip-http-etag', '--cache-trust'))
        assert third['hits'] > 0 and 'partial verification' in text
        # The versioned disk format must work across the GNU and MSVC builds.
        native = subprocess.run([str(args.checker.resolve()), base + '/ok/sample.zip?sig=cache-agent-secret',
                                 str(work / 'cache-sample.zip')], capture_output=True, check=True, timeout=20)
        assert hashlib.sha256(files['sample.zip'][:4 << 20]).hexdigest().encode() in native.stdout
        again, text = run('portable-cache-msvc-reader', 'sample.zip')
        assert again['downloaded'] == 1 and again['hits'] > 0 and 'Cache verified' in text
    finally:
        server.shutdown(); server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} cache Agent scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
