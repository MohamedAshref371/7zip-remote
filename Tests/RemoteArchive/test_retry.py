"""Fault injection for bounded HTTP retries, archive identity and cancellation."""
import argparse
import email.utils
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
    work = Path(tempfile.mkdtemp(prefix='retry-', dir=ROOT / '.build/msvc/runs'))
    target = b'retried archive member\n' * 100
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('000-first.bin', random.Random(55).randbytes(2 << 20))
        z.writestr('docs/target.txt', target)
        z.writestr('999-last.bin', random.Random(56).randbytes(2 << 20))
    server = FixtureServer({'sample.zip': archive.getvalue()})
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, fault=None, *, success=True, options=(), frontend=None):
        if fault is not None:
            server.failures[label] = fault
        first = len(server.records)
        name = 'sample.zip' if frontend else 'pattern.bin'
        url = f'{base}/{label}/{name}?sig=retry-secret%2F&x=1&x=2'
        out = work / label
        if frontend == 'cli':
            command = [str(exe), 'x', url, 'docs/target.txt', '-o' + str(out), '-y', '-bd', '-sccUTF-8', *options]
        else:
            command = [str(agent), url, str(out), *([] if frontend else ['--transport-retry-check']), *options]
        started = time.monotonic()
        result = subprocess.run(command, capture_output=True, timeout=12)
        elapsed = time.monotonic() - started
        text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(text, encoding='utf-8')
        assert (result.returncode == 0) == success, (label, result.returncode, text)
        assert 'retry-secret' not in text, (label, text)
        records = server.records[first:]
        unchecked = '--skip-http-etag' in options
        assert all(r['if_match'] == (None if unchecked or (r['start'], r['end']) == (0, 0)
                                     else '"fixture-v1"') for r in records), (label, records)
        assert all(r['encoding'] == 'identity' and r['cookie'] is None and
                   r['authorization'] is None and r['referer'] is None for r in records)
        if frontend and success:
            selected = out / ('docs/target.txt' if frontend == 'cli' else 'target.txt')
            assert selected.read_bytes() == target
            if frontend == 'agent':
                stats = re.findall(r'NETWORK (?:open|extract|test) (\d+) (\d+) (\d+)', text)
                assert len(stats) == 3 and sum(int(s[2]) for s in stats) == len(records), (label, text)
        cases.append(dict(label=label, seconds=round(elapsed, 3), requests=len(records)))
        print('PASS ' + label, flush=True)
        return records, elapsed, text

    try:
        for phase in ('probe', 'range'):
            for status in (408, 429, 500, 502, 503, 504):
                records, _, _ = run(f'{phase}-{status}', dict(phase=phase, status=status))
                faults = [r for r in records if ((r['start'], r['end']) == (0, 0)) == (phase == 'probe')]
                assert len(faults) == 2 and faults[1]['at'] - faults[0]['at'] >= 0.23

        records, _, _ = run('two-failures', dict(times=2))
        assert len(records) == 4 # three probes, then one data range
        assert records[2]['at'] - records[1]['at'] >= 0.48
        records, _, _ = run('retry-budget', dict(times=100), success=False)
        assert len(records) == 3
        for status in (400, 401, 403, 404, 407, 410, 412, 416, 501, 507):
            records, _, _ = run('no-retry-status-' + str(status), dict(status=status), success=False)
            assert len(records) == 1

        for kind in ('short-body', 'short-chunked'):
            records, _, text = run(kind, dict(phase='range', kind=kind))
            assert len(records) == 3
            assert [(r['start'], r['end']) for r in records[1:]] == [(0, 262143)] * 2
            match = re.search(r'RETRY_TRANSPORT 00000000 (\d+) (\d+) 206', text)
            assert match and int(match[2]) == 1 + 131072 + 262144, text
        run('disconnect-before-headers', dict(kind='disconnect'))
        records, _, _ = run('receive-timeout', dict(kind='timeout'), options=('--retry-timeout-ms=200',))
        assert len(records) == 3 and server.disconnected.wait(1)

        # No retry for identity/range/policy failures, even after a recoverable fault.
        for mode in ('changed', 'changed-size', 'wrong-range', 'missing-range', 'duplicate-range',
                     'missing-etag', 'duplicate-etag', 'weak', 'encoded', 'multipart', 'wrong-length',
                     'extra-body', 'ignore', 'precondition', 'unauthorized', 'forbidden', 'missing',
                     'proxy-auth', 'range416', 'gone', 'unexpected-status'):
            records, _, _ = run('reject-' + mode, dict(phase='range', times=0, after_mode=mode), success=False)
            assert len(records) in (1, 2), (mode, records)
        for mode in ('changed', 'changed-size', 'wrong-range', 'precondition'):
            records, _, _ = run('after-fault-' + mode, dict(phase='range', after_mode=mode), success=False)
            assert len(records) == 3

        run('bypass-recovery', dict(phase='range'), options=('--skip-http-etag',))
        records, _, _ = run('bypass-size-change', dict(phase='range', after_mode='changed-size'),
                            success=False, options=('--skip-http-etag',))
        assert len(records) == 3

        for value in ('1', '0'):
            records, _, _ = run('retry-after-' + value, dict(retry_after=[value]))
            assert records[1]['at'] - records[0]['at'] >= (0.98 if value == '1' else 0.23)
        date = email.utils.formatdate(time.time() + 2, usegmt=True)
        date_deadline = time.monotonic() + email.utils.parsedate_to_datetime(date).timestamp() - time.time()
        records, _, _ = run('retry-after-date', dict(retry_after=[date]))
        assert records[1]['at'] >= date_deadline - 0.02
        for label, values in [('long', ['60']), ('overflow', ['999999999999999999999999']),
                              ('invalid', ['later']), ('negative', ['-1']), ('duplicate', ['0', '0'])]:
            records, elapsed, _ = run('retry-after-' + label, dict(retry_after=values), success=False)
            assert len(records) == 1 and elapsed < 1.5

        records, elapsed, _ = run('shared-deadline', dict(times=100), success=False,
                                  options=('--retry-deadline-ms=700',))
        assert len(records) == 2 and elapsed < 1.5

        server.redirects['via-redirect'] = (302, ['/redirect-target/pattern.bin?sig=retry-secret'])
        server.failures['redirect-target'] = dict(phase='range')
        records, _, _ = run('via-redirect')
        assert len(records) == 4 and sum(r['mode'] == 'via-redirect' for r in records) == 1

        for frontend in ('cli', 'agent'):
            run(frontend + '-extract', dict(phase='range'), frontend=frontend)
            run(frontend + '-partial-recovery', dict(phase='range', kind='short-body'), frontend=frontend)

        # Cancel during Retry-After, before another request can be sent.
        records, elapsed, text = run('cancel-backoff', dict(times=100, retry_after=['2']), success=False,
                                     frontend='agent', options=('--cancel-open-ms=300',))
        assert len(records) == 1 and elapsed < 1.5 and 'Cancelled.' in text
        server.failures['event-backoff'] = dict(times=100, retry_after=['2'])
        first = len(server.records)
        result = subprocess.run([str(agent), base + '/event-backoff/pattern.bin', str(work),
                                 '--transport-repeats=1', '--cancel-event'], capture_output=True, timeout=4)
        assert result.returncode == 0 and b'CANCEL_CHECK 2' in result.stdout, result
        assert len(server.records[first:]) == 2
        cases.append(dict(label='cancel-event-backoff'))
        print('PASS cancel-event-backoff', flush=True)
    finally:
        server.release_holds.set()
        server.shutdown()
        server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} retry scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
