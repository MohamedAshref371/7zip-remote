"""Test the unified remote RAR choice using caller-supplied RAR4/RAR5 fixtures.

No fixtures are downloaded or redistributed. Uses the actual File Manager Agent
and compares extracted files against normal local 7-Zip extraction.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import threading

from test_remote import FixtureServer, ROOT


def manifest(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / '.build/msvc/rar-family/agentcheck.exe')
    parser.add_argument('--rar4', type=Path, required=True)
    parser.add_argument('--rar5', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    runs = ROOT / '.build/msvc/runs'
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='rar-family-', dir=runs))
    archives = {'legacy': args.rar4.resolve(), 'modern': args.rar5.resolve()}
    files = {}
    fixtures = {}
    for name, path in archives.items():
        blob = path.read_bytes()
        files[name + '.rar'] = files[name + '.bin'] = blob
        reference = work / (name + '-reference')
        subprocess.run([str(exe.with_name('7z.exe')), 'x', str(path), '-o' + str(reference), '-y', '-bd'],
                       capture_output=True, check=True, timeout=60)
        fixtures[name] = dict(sha256=hashlib.sha256(blob).hexdigest(), bytes=len(blob),
                              files=manifest(reference))
        assert fixtures[name]['files']
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []
    try:
        for name in archives:
            for label, mode, extension, options in [
                ('auto', 'ok', '.rar', []),
                ('family', 'ok', '.rar', ['--format=rar']),
                ('family-unknown-extension', 'ok', '.bin', ['--format=rar']),
                ('family-without-etag', 'missing-etag', '.rar', ['--format=rar', '--skip-http-etag'])]:
                output = work / (name + '-' + label)
                first = len(server.records)
                proc = subprocess.run([str(exe), f'{base}/{mode}/{name}{extension}', str(output),
                                       '--rar-family-check', *options], capture_output=True, timeout=60)
                text = (proc.stdout + proc.stderr).decode('utf-8', errors='replace')
                assert proc.returncode == 0, (name, label, text)
                expected = 'rar' if name == 'legacy' else 'rar5'
                handler = next(line.split('=', 1)[1].strip().lower()
                               for line in text.splitlines() if line.startswith('Handler='))
                assert handler == expected, (name, label, handler)
                assert manifest(output) == fixtures[name]['files'], (name, label, 'output mismatch')
                requests = server.records[first:]
                for request in requests:
                    if options and '--skip-http-etag' in options:
                        assert request['if_match'] is None
                    elif (request['start'], request['end']) != (0, 0):
                        assert request['if_match'] == '"fixture-v1"'
                cases.append(dict(label=name + '-' + label, handler=handler, requests=len(requests),
                                  output_hashes_match=True))
                print('PASS ' + cases[-1]['label'] + ': ' + handler, flush=True)
    finally:
        server.shutdown()
        server.server_close()
        report = dict(cases=cases, fixtures=fixtures, executable=str(exe),
                      executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                      desktop_ui_verified=False)
        (work / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print('Report: ' + str(work / 'report.json'), flush=True)
    print(f'All {len(cases)} RAR-family scenarios passed.')


if __name__ == '__main__':
    main()
