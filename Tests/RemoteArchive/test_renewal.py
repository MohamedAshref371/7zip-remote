"""Renew an expired archive URL through the real Agent, preserving cache policy."""
import argparse
import io
import json
from pathlib import Path
import random
import subprocess
import tempfile
import threading
import zipfile

from test_remote import FixtureServer, Handler, ROOT


class RenewalHandler(Handler):
    def do_GET(self):
        mode = self.path.strip('/').split('/')[0]
        # The Agent triggers expiration only after it has opened the archive.
        if mode.startswith('expire-') and self.headers.get('Range') == 'bytes=12345-12345':
            self.server.failures[mode] = dict(phase='range', status=int(mode[7:]), times=1000)
        super().do_GET()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='renewal-', dir=ROOT / '.build/msvc/runs'))
    target = random.Random(52).randbytes(700000)

    def archive(data):
        out = io.BytesIO()
        with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:
            z.writestr('000-first.bin', bytes(2 << 20))
            z.writestr('docs/target.txt', data)
            z.writestr('999-last.bin', bytes(2 << 20))
        return out.getvalue()

    changed_target = bytes([target[0] ^ 1]) + target[1:]
    files = {'sample.zip': archive(target), 'changed.zip': archive(changed_target), 'bad.zip': b'not an archive'}
    inputs = work / 'input' / 'docs'
    inputs.mkdir(parents=True)
    (inputs / 'target.txt').write_bytes(target)
    for name, options in [('sample.7z', ['-ms=off']), ('secret.7z', ['-mhe=on', '-pfixture-password'])]:
        subprocess.run([str(exe.with_name('7z.exe')), 'a', str(work / name), *options, '.'],
                       cwd=inputs.parent, capture_output=True, check=True)
        files[name] = (work / name).read_bytes()
    server = FixtureServer(files)
    server.RequestHandlerClass = RenewalHandler
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, *, name='sample.zip', mode='ok', destination='ok', new_name=None,
            options=(), enabled=True, expected=target, fail=False):
        server.failures.pop(mode, None)
        server.failure_counts.pop(mode, None)
        cache = work / ('cache-' + label)
        old = f'{base}/{mode}/{name}?sig=old-renew-secret'
        new = f'{base}/{destination}/{new_name or name}?sig=new-renew-secret'
        out = work / ('output-' + label)
        command = [str(exe), old, str(out), '--renew-url=' + new, *options]
        if enabled:
            command.append('--cache-dir=' + str(cache))
        if fail:
            command.append('--renew-fails')
        result = subprocess.run(command, capture_output=True, timeout=40)
        text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(text, encoding='utf-8')
        assert result.returncode == 0, (label, result.returncode, text)
        assert 'old-renew-secret' not in text and 'new-renew-secret' not in text
        assert ('RENEW_ROLLBACK_OK' if fail else 'RENEW_OK') in text
        if fail and mode.startswith('expire-'):
            assert 'RENEW_EXPIRED_VIEW_PRESERVED' in text
        else:
            assert (out / 'target.txt').read_bytes() == expected
        if mode.startswith('expire-'):
            assert 'EXPIRED ' + mode[7:] in text
        cases.append(label)
        print('PASS ' + label, flush=True)

    try:
        for status in (403, 404, 410):
            run('expired-' + str(status), mode='expire-' + str(status),
                options=('--expire-check', '--renew-consent', '--renew-reuse'))
        run('without-consent-even-if-remembered', options=('--cache-trust',))
        run('no-disk-cache', enabled=False)
        run('changed-content-isolated', new_name='changed.zip', expected=changed_target,
            options=('--renew-consent',))
        run('new-link-denied-rollback', destination='forbidden', fail=True, options=('--renew-consent',))
        run('expired-new-link-denied', mode='expire-403', destination='forbidden', fail=True,
            options=('--expire-check', '--renew-consent'))
        run('cancel-renewal-rollback', fail=True, options=('--renew-cancel', '--renew-consent'))
        run('invalid-archive-rollback', new_name='bad.zip', fail=True, options=('--renew-consent',))
        run('missing-etag-strict-rollback', destination='missing-etag', fail=True,
            options=('--renew-consent',))
        run('etag-bypass-consented', mode='missing-etag', destination='missing-etag',
            options=('--skip-http-etag', '--renew-consent', '--renew-reuse'))
        run('etag-bypass-no-consent', mode='missing-etag', destination='missing-etag',
            options=('--skip-http-etag', '--cache-trust'))
        run('7z-consented', name='sample.7z', options=('--renew-consent', '--renew-reuse'))
        run('encrypted-consented', name='secret.7z', options=('--renew-consent', '--renew-reuse'))
    finally:
        server.shutdown()
        server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2) + '\n', encoding='utf-8')
    print(f'All {len(cases)} renewal scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
