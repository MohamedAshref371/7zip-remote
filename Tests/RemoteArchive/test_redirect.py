"""Exercise redirects through the real console and File Manager Agent."""
import argparse
import io
import json
from pathlib import Path
import random
import re
import subprocess
import tempfile
import threading
import time
import zipfile

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--agent', type=Path, required=True)
    args = parser.parse_args()
    exe, agent = args.exe.resolve(), args.agent.resolve()
    work = Path(tempfile.mkdtemp(prefix='redirect-', dir=ROOT / '.build/msvc/runs'))
    target = b'redirected archive member\n' * 100
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('000-first.bin', random.Random(44).randbytes(2 << 20))
        z.writestr('docs/target.txt', target)
        z.writestr('999-last.bin', random.Random(45).randbytes(2 << 20))
    servers = [FixtureServer({'sample.zip': archive.getvalue()}) for _ in range(2)]
    for server in servers:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    origin, destination = servers
    base = f'http://127.0.0.1:{origin.server_port}'
    other = f'http://127.0.0.1:{destination.server_port}'
    signed = '?sig=redirect-secret%2F%2B&x=1&x=2'
    cases = []

    def run(label, mode, *, success=True, hint='', agent_mode=False, options=()):
        before = [len(s.records) for s in servers]
        out = work / label
        url = f'{base}/{mode}/sample.zip?sig=original-secret'
        command = ([str(agent), url, str(out), *options] if agent_mode else
                   [str(exe), 'x', url, 'docs/target.txt', '-o' + str(out), '-y', '-bd',
                    '-sccUTF-8', *options])
        started = time.monotonic()
        result = subprocess.run(command, capture_output=True, timeout=10)
        elapsed = time.monotonic() - started
        text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(text, encoding='utf-8')
        assert (result.returncode == 0) == success, (label, result.returncode, text)
        assert hint in text, (label, hint, text)
        assert 'redirect-secret' not in text and 'original-secret' not in text, (label, text)
        records = [r for s, first in zip(servers, before) for r in s.records[first:]]
        assert all(r['cookie'] is None and r['authorization'] is None and r['referer'] is None
                   for r in records), (label, records)
        assert all(r['encoding'] == 'identity' for r in records), (label, records)
        if success:
            selected = out / ('target.txt' if agent_mode else 'docs/target.txt')
            assert selected.read_bytes() == target, label
            if agent_mode:
                stats = re.findall(r'NETWORK (?:open|extract|test) (\d+) (\d+) (\d+)', text)
                assert len(stats) == 3, (label, text)
                assert sum(int(s[2]) for s in stats) == len(records), (label, stats, records)
                payload = [r for r in records if r['mode'] not in origin.redirects or
                           (r['mode'] in origin.redirect_after_probe and (r['start'], r['end']) == (0, 0))]
                assert sum(int(s[0]) for s in stats) == sum(r['end'] - r['start'] + 1 for r in payload)
        cases.append(dict(label=label, requests=len(records), seconds=round(elapsed, 3)))
        print('PASS ' + label, flush=True)
        return records, elapsed

    try:
        for status in (301, 302, 303, 307, 308):
            mode = 'status' + str(status)
            origin.redirects[mode] = (status, ['/ok/sample.zip' + signed])
            records, _ = run(mode, mode)
            # Follow once while probing, then reuse the resolved destination.
            assert sum(r['mode'] == mode for r in records) == 1
            assert all(r['path'] == '/ok/sample.zip' + signed for r in records[1:])
            assert all(r['if_match'] == '"fixture-v1"' for r in records[2:])

        targets = {
            'absolute': other + '/ok/sample.zip' + signed,
            'network-path': f'//127.0.0.1:{destination.server_port}/ok/sample.zip' + signed,
            'relative': '../ok/sample.zip' + signed,
            'dot-path': './../ok/./sample.zip' + signed,
            'fragment': '/ok/sample.zip' + signed + '#not-sent',
        }
        for mode, location in targets.items():
            origin.redirects[mode] = (302, [location])
            records, _ = run(mode, mode, agent_mode=True)
            assert any(r['path'] == '/ok/sample.zip' + signed for r in records)
            assert all('#' not in r['path'] for r in records)

        # Exactly five hops succeed; a sixth is rejected before contacting its target.
        for i in range(6):
            origin.redirects['hop' + str(i)] = (302, [
                f'/{"ok" if i == 5 else "hop" + str(i + 1)}/sample.zip'])
        run('five-hops', 'hop1')
        records, _ = run('six-hops-rejected', 'hop0', success=False, hint='Too many HTTP redirects')
        assert len(records) == 6 and not any(r['mode'] == 'ok' for r in records)

        for mode, locations, hint in [
            ('missing', [], 'Location'),
            ('duplicate', ['/ok/sample.zip', '/ok/sample.zip'], 'Location'),
            ('empty', [''], 'Location'),
            ('credentials', [other.replace('http://', 'http://user:secret@') + '/ok/sample.zip'], 'credentials'),
            ('empty-credentials', [other.replace('http://', 'http://@') + '/ok/sample.zip'], 'credentials'),
            ('bad-scheme', ['ftp://127.0.0.1/sample.zip'], 'HTTP(S)'),
            ('bad-space', ['/ok/sample zip'], 'percent-encoded'),
            ('bad-slash', [r'/ok\sample.zip'], 'percent-encoded'),
        ]:
            origin.redirects[mode] = (302, locations)
            records, _ = run('reject-' + mode, mode, success=False, hint=hint)
            assert len(records) == 1

        origin.redirects['unsupported'] = (305, ['/ok/sample.zip'])
        run('unsupported-status', 'unsupported', success=False, hint='Unsupported HTTP redirect status')
        origin.redirects['loop'] = (302, ['sample.zip'])
        records, _ = run('loop', 'loop', success=False, hint='Too many HTTP redirects')
        assert len(records) == 6
        origin.redirects['query'] = (302, ['?replacement=redirect-secret%2F&x=1&x=2'])
        records, _ = run('query-only', 'query', success=False, hint='Too many HTTP redirects')
        assert all(r['path'] == '/query/sample.zip?replacement=redirect-secret%2F&x=1&x=2'
                   for r in records[1:])
        for mode in ('changed', 'changed-size', 'wrong-range', 'ignore', 'missing-etag'):
            origin.redirects['to-' + mode] = (302, [other + '/' + mode + '/sample.zip'])
            run('validate-' + mode, 'to-' + mode, success=False)
        run('etag-bypass', 'to-missing-etag', options=('-shttp-no-etag',))
        run('etag-bypass-keeps-size-check', 'to-changed-size', success=False, options=('-shttp-no-etag',))

        # A source can redirect later data ranges, after its identity was established.
        for final in ('ok', 'changed', 'changed-size'):
            mode = 'late-' + final
            origin.redirects[mode] = (307, [other + '/' + final + '/sample.zip'])
            origin.redirect_after_probe.add(mode)
            run(mode, mode, success=final == 'ok', agent_mode=final == 'ok')

        # Cancellation must still abort a stalled destination after a cross-host hop.
        origin.redirects['cancel'] = (302, [other + '/hang-headers/sample.zip'])
        _, elapsed = run('cancel-after-redirect', 'cancel', success=False, hint='Cancelled.',
                         agent_mode=True, options=('--cancel-open-ms=300',))
        assert elapsed < 1.5 and destination.held.is_set()
        assert destination.disconnected.wait(1)

        # A hop must not restart the full range-request deadline.
        for i in range(5):
            origin.redirects['deadline' + str(i)] = (302, [
                f'/{"ok" if i == 4 else "deadline" + str(i + 1)}/sample.zip'])
            origin.redirect_delays['deadline' + str(i)] = 0.1
        result = subprocess.run([str(agent), base + '/deadline0/sample.zip', str(work),
                                 '--transport-deadline-check'], capture_output=True, timeout=3)
        assert result.returncode == 0 and b'REDIRECT_DEADLINE_OK' in result.stdout, result
        cases.append(dict(label='shared-request-deadline'))
        print('PASS shared-request-deadline', flush=True)
    finally:
        for server in servers:
            server.release_holds.set()
            server.shutdown()
            server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} redirect scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
