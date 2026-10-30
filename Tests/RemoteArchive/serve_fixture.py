"""Serve disposable archive fixtures on loopback for manual File Manager tests.

Run from the repository root. Prints direct URLs and a test-only password.
Ctrl+C stops the server. Artifacts and a request log remain under .build/.
"""
import argparse
import json
from pathlib import Path
import random
import subprocess
import tempfile
import zipfile

from test_remote import FixtureServer, Handler, ROOT


class RecordingHandler(Handler):
    def do_GET(self):
        try:
            super().do_GET()
        finally:
            with self.server.lock:
                self.server.record_path.write_text(
                    json.dumps(self.server.records, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sevenzip', type=Path, default=ROOT / '.build/msvc/bin/7z.exe')
    args = parser.parse_args()
    sevenzip = args.sevenzip.resolve()
    if not sevenzip.is_file():
        raise SystemExit('Build the MSVC binaries first: python Tests/RemoteArchive/build_msvc.py --http')
    runs = ROOT / '.build/msvc/runs'
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='manual-', dir=runs))
    inputs = work / 'input'
    (inputs / 'docs').mkdir(parents=True)
    (inputs / 'docs/target.txt').write_bytes(b'Manual remote extraction test.\n' * 100)
    (inputs / 'hello.txt').write_bytes(b'Hello from an HTTP archive.\n')
    (inputs / 'unicode-\u0645\u0644\u0641.txt').write_bytes(b'Unicode filename test.\n')
    (inputs / '000-data.bin').write_bytes(random.Random(31).randbytes(4 << 20))
    archive = work / 'sample.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(inputs.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(inputs).as_posix())
    password = 'manual-test'
    subprocess.run([str(sevenzip), 'a', str(work / 'secret.7z'), '-t7z', '-ms=off',
                    '-mhe=on', '-p' + password, '.'], cwd=inputs, check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    files = {name: (work / name).read_bytes() for name in ('sample.zip', 'secret.7z')}
    server = FixtureServer(files)
    server.RequestHandlerClass = RecordingHandler
    server.record_path = work / 'requests.json'
    server.record_path.write_text('[]\n', encoding='utf-8')
    base = f'http://127.0.0.1:{server.server_port}'
    session = dict(
        zip_url=base + '/ok/sample.zip', encrypted_url=base + '/ok/secret.7z',
        slow_url=base + '/slow/sample.zip', rejected_url=base + '/ignore/sample.zip',
        no_etag_url=base + '/missing-etag/sample.zip',
        redirect_url=base + '/redirect/sample.zip',
        password=password, directory=str(work), requests=str(server.record_path))
    # Stable discovery file for the current manual session. Payloads stay unique.
    (ROOT / '.build/msvc/manual-session.json').write_text(
        json.dumps(session, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(session, indent=2), flush=True)
    print('Listening on 127.0.0.1 only. Ctrl+C stops the manual test server.', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
