"""Bound cancellation latency during stalled headers/body and check cleanup."""
import argparse
import io
import json
from pathlib import Path
import random
import subprocess
import tempfile
import threading
import time
import zipfile

from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='cancel-', dir=ROOT / '.build/msvc/runs'))
    data = random.Random(92).randbytes(4 << 20)
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('000-first.bin', data)
        z.writestr('docs/target.txt', data[:2 << 20])
        z.writestr('999-last.bin', data)
    cases = []
    for kind, mode, event_mode in [
        ('open', 'hang-headers', False), ('open', 'hang-body', False),
        ('extract', 'extract-hang-headers', False), ('extract', 'extract-hang-body', False),
        ('stress', 'hang-headers', False), ('stress', 'hang-body', False),
        ('stress', 'hang-headers', True), ('stress', 'hang-body', True),
    ]:
        label = kind + '-' + mode + ('-event' if event_mode else '')
        server = FixtureServer({'sample.zip': archive.getvalue()})
        threading.Thread(target=server.serve_forever, daemon=True).start()
        url = f'http://127.0.0.1:{server.server_port}/{mode}/sample.zip?sig=cancel-secret'
        output = work / label
        output.mkdir()
        sentinel = output / 'existing.txt'
        sentinel.write_bytes(b'preserve existing file')
        options = ['--cancel-open-ms=300'] if kind == 'open' else ['--cancel-extract-ms=300']
        if kind == 'stress':
            options = ['--transport-repeats=16'] + (['--cancel-event'] if event_mode else [])
        process = None
        try:
            started = time.monotonic()
            if kind == 'extract':
                process = subprocess.Popen([str(exe), url, str(output), *options], stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                ready = threading.Event()
                lines = []
                def read_output():
                    for line in process.stdout:
                        lines.append(line)
                        if line.strip() == 'EXTRACT_READY':
                            ready.set()
                reader = threading.Thread(target=read_output, daemon=True)
                reader.start()
                assert ready.wait(3), (label, 'opening did not finish', lines)
                server.hold_enabled = True
                started = time.monotonic()
                process.stdin.write('g\n')
                process.stdin.flush()
                process.stdin.close()
                process.wait(timeout=4)
                reader.join(1)
                text = ''.join(lines) + process.stderr.read()
                code = process.returncode
            else:
                result = subprocess.run([str(exe), url, str(output), *options],
                                        capture_output=True, text=True, timeout=8)
                text, code = result.stdout + result.stderr, result.returncode
            elapsed = time.monotonic() - started
            (work / (label + '.log')).write_text(text, encoding='utf-8')
            assert code == (0 if kind == 'stress' else 3), (label, code, text)
            assert elapsed < (7 if kind == 'stress' else 1.5), (label, elapsed, text)
            assert server.held.is_set(), (label, 'test did not reach a blocked network operation')
            assert server.disconnected.wait(1), (label, 'request connection was not closed')
            deadline = time.monotonic() + 1
            expected_closes = 17 if kind == 'stress' else 1
            while server.closed_holds < expected_closes and time.monotonic() < deadline:
                time.sleep(0.01)
            assert server.closed_holds == expected_closes, (label, server.closed_holds, text)
            assert 'cancel-secret' not in text
            if kind != 'stress':
                assert 'Cancelled.' in text, (label, text)
            assert sentinel.read_bytes() == b'preserve existing file'
            assert not list(output.glob('*.tmp'))
            target = output / 'target.txt'
            if target.exists():
                # Preserve 7-Zip's partial-output policy, but verify its handle
                # was closed and that cancellation did not leave it locked.
                moved = output / 'closed-output.txt'
                target.rename(moved)
                moved.rename(target)
            cases.append(dict(label=label, seconds=round(elapsed, 3), connection_closed=True))
            print(f'PASS {label}: {elapsed:.3f}s', flush=True)
        finally:
            server.release_holds.set()
            if process and process.poll() is None:
                process.kill()
                process.wait()
            server.shutdown()
            server.server_close()
            (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} cancellation scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
