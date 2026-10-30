"""Exercise File Manager's compiled Agent without desktop UI automation."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import subprocess
import tempfile
import threading
import tarfile
import zipfile

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=ROOT / '.build/msvc/bin/agentcheck.exe')
    args = parser.parse_args()
    exe = args.exe.resolve()
    runs = ROOT / '.build/msvc/runs'
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='agent-', dir=runs))
    inputs = work / 'input'
    (inputs / 'docs').mkdir(parents=True)
    target = b'File Manager Agent remote extraction\n' * 100
    (inputs / 'docs/target.txt').write_bytes(target)
    (inputs / '000-data.bin').write_bytes(random.Random(12).randbytes(4 << 20))
    with zipfile.ZipFile(work / 'sample.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(inputs / '000-data.bin', '000-data.bin')
        z.writestr('docs/target.txt', target)
    with tarfile.open(work / 'sample.tar', 'w') as tar:
        tar.add(inputs / 'docs/target.txt', arcname='docs/target.txt')
    with zipfile.ZipFile(work / 'middle.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(inputs / '000-data.bin', '000-first.bin')
        z.writestr('docs/target.txt', target)
        z.write(inputs / '000-data.bin', '999-last.bin')
    for name, flags in [('nonsolid.7z', ['-ms=off']), ('solid.7z', ['-ms=on']),
                        ('secret.7z', ['-mhe=on', '-pfixture-password', '-ms=off'])]:
        subprocess.run([str(exe.with_name('7z.exe')), 'a', str(work / name), '-t7z', *flags, '.'],
                       cwd=inputs, check=True, capture_output=True)
    files = {p.name: p.read_bytes() for p in work.iterdir() if p.suffix in ('.zip', '.7z', '.tar')}
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, source, expected=0, extra=()):
        output_dir = work / label
        first_request = len(server.records)
        result = subprocess.run([str(exe), str(source), str(output_dir), *extra], capture_output=True, timeout=60)
        text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(text, encoding='utf-8')
        assert result.returncode == expected, (label, result.returncode, text)
        assert 'fixture-secret' not in text
        ranges = server.records[first_request:]
        cases.append(dict(label=label, exit_code=result.returncode, requests=len(ranges),
                          requested_bytes=sum(r['end'] - r['start'] + 1 for r in ranges)))
        if expected == 0:
            assert (output_dir / 'target.txt').read_bytes() == target
            if str(source).startswith('http'):
                assert b'ZoneId=3' in Path(str(output_dir / 'target.txt') + ':Zone.Identifier').read_bytes()
                stats = re.findall(r'NETWORK (open|extract|test) (\d+) (\d+) (\d+)', text)
                assert [s[0] for s in stats] == ['open', 'extract', 'test'], text
                assert all(int(s[2]) == len(files[source.split('?')[0].rsplit('/', 1)[1]])
                           for s in stats), stats
                assert sum(int(s[1]) for s in stats) == sum(
                    r['end'] - r['start'] + 1 for r in ranges), (stats, ranges)
                assert sum(int(s[3]) for s in stats) == len(ranges), (stats, ranges)
                estimates = re.findall(r'ESTIMATE (open|extract|test) (\d+) (\d+)', text)
                assert [s[0] for s in estimates] == ['open', 'extract', 'test'], text
                name = source.split('?')[0].rsplit('/', 1)[1]
                assert estimates[0][1] == '0'  # no selected files while opening
                supported = name.endswith(('.zip', '.7z'))
                assert all((s[1] == '1') == supported for s in estimates[1:]), estimates
                if supported:
                    assert all(int(s[2]) <= len(files[name]) for s in estimates[1:]), estimates
                    if name.endswith('.7z'):
                        # These fixtures fit in cache; a second test needs no download.
                        assert estimates[2][2] == '0', estimates
                    if name == 'solid.7z':
                        assert int(estimates[1][2]) >= int(stats[1][1]) > 0, (stats, estimates)
                    if name == 'middle.zip':
                        assert int(estimates[1][2]) >= int(stats[1][1]) > 0, (stats, estimates)
                        assert int(estimates[1][2]) <= int(stats[1][1]) + 524288, (stats, estimates)
            else:
                assert 'NETWORK ' not in text, text
        print('PASS ' + label, flush=True)
        return text

    try:
        for name in files:
            run(name + '-local', work / name)
            run(name + '-remote', f'{base}/ok/{name}?sig=fixture-secret')
        run('fresh-session', f'{base}/ok/sample.zip?sig=fixture-secret')
        before = len(server.records)
        run('cancel-before', f'{base}/ok/sample.zip', 3, ['cancel'])
        assert len(server.records) == before
        # These probe failures use the same error path displayed by File Manager.
        for mode, hint in {
            'unauthorized': 'Browser login sessions are not supported',
            'forbidden': 'obtain a fresh direct download URL',
            'gone': 'Check the URL',
            'proxy-auth': 'proxy authentication is not supported',
            'rate-limit': 'Wait before reopening',
            'server-error': 'Try reopening the archive later',
            'unexpected-status': 'Unexpected HTTP status 418',
        }.items():
            text = run('error-hint-' + mode, f'{base}/{mode}/sample.zip?sig=fixture-secret', 1)
            assert hint in text, (mode, text)
        for mode in ('ignore', 'missing-etag', 'weak', 'changed', 'changed-size', 'precondition',
                     'truncated', 'redirect', 'forbidden', 'missing', 'range416'):
            text = run(mode, f'{base}/{mode}/sample.zip?sig=fixture-secret', 1)
            if mode in ('forbidden', 'missing', 'range416'):
                assert str({'forbidden': 403, 'missing': 404, 'range416': 416}[mode]) in text
        assert any(r['path'].endswith('?sig=fixture-secret') for r in server.records)
        strict_end = len(server.records)
        assert all(r['if_match'] == '"fixture-v1"' for r in server.records
                   if (r['start'], r['end']) != (0, 0))
        for mode in ('missing-etag', 'weak', 'duplicate-etag', 'changed'):
            run('no-etag-' + mode, f'{base}/{mode}/sample.zip', extra=['--skip-http-etag'])
        run('no-etag-encrypted', f'{base}/missing-etag/secret.7z', extra=['--skip-http-etag'])
        run('no-etag-size-rejected', f'{base}/changed-size/sample.zip', 1, ['--skip-http-etag'])
        before = len(server.records)
        run('no-etag-cancel-before', f'{base}/missing-etag/sample.zip', 3, ['cancel', '--skip-http-etag'])
        assert len(server.records) == before
        assert all(r['if_match'] is None for r in server.records[strict_end:])
        run('default-still-strict', f'{base}/missing-etag/sample.zip', 1)
        run('selected-zip', f'{base}/ok/sample.zip', extra=['--format=zip'])
        run('selected-7z', f'{base}/ok/nonsolid.7z', extra=['--format=7z'])
        run('selected-encrypted-7z', f'{base}/ok/secret.7z', extra=['--format=7z'])
        run('selected-zip-no-etag', f'{base}/missing-etag/sample.zip',
            extra=['--format=zip', '--skip-http-etag'])
        run('selected-wrong-format', f'{base}/ok/sample.zip', 1, ['--format=7z'])
        run('rar-family-rejects-zip', f'{base}/ok/sample.zip', 1, ['--format=rar'])
    finally:
        server.shutdown()
        server.server_close()
        report = dict(cases=cases, requests=server.records, executable=str(exe),
                      executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                      engine_sha256=hashlib.sha256(exe.with_name('7z.dll').read_bytes()).hexdigest(),
                      ui_verified=False)
        (work / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print('Report: ' + str(work / 'report.json'), flush=True)
    print(f'All {len(cases)} Agent scenarios passed.')


if __name__ == '__main__':
    main()
