"""Validate compressed RAR estimates using caller-supplied libarchive fixtures.

No network downloads occur. See Notes/16-rar-iso-estimates.md for provenance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import threading

from test_remote import FixtureServer, ROOT


def hashes(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--fixtures-dir', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='rar-estimates-', dir=ROOT / '.build/msvc/runs'))
    fixtures = [
        ('test_read_format_rar_compress_normal', None, False),
        ('test_read_format_rar5_compressed', None, False),
        ('test_read_format_rar5_multiple_files_solid', None, True),
        ('test_read_format_rar4_solid_encrypted', 'password', True),
        ('test_read_format_rar4_solid_encrypted_filenames', 'password', True),
    ]
    files = {name + '.rar': (args.fixtures_dir / (name + '.rar')).read_bytes()
             for name, _, _ in fixtures}
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []
    try:
        for name, password, solid in fixtures:
            source = (args.fixtures_dir / (name + '.rar')).resolve()
            reference = work / (name + '-reference')
            password_option = ['-p' + password] if password else []
            # This legacy fixture also contains a Unix symlink. Its extraction
            # requires a Windows privilege unrelated to read-range estimation.
            exclude = ['-x!testlink'] if name == 'test_read_format_rar_compress_normal' else []
            subprocess.run([str(exe.with_name('7z.exe')), 'x', str(source), '-y', '-bd',
                            '-o' + str(reference), *password_option, *exclude], check=True, capture_output=True, timeout=60)
            expected = hashes(reference)
            assert expected
            for mode in ('ok', 'missing-etag'):
                label = name + '-' + mode
                output = work / label
                options = ['--estimate-archive-check', '--format=rar']
                if exclude:
                    options += ['--skip-item=testlink']
                if mode == 'missing-etag':
                    options += ['--skip-http-etag']
                if password:
                    options += ['--password=' + password]  # public test-fixture password
                first = len(server.records)
                result = subprocess.run([str(exe), f'{base}/{mode}/{name}.rar?sig=fixture-secret',
                                         str(output), *options], capture_output=True, timeout=60)
                text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                (work / (label + '.log')).write_text(text, encoding='utf-8')
                assert result.returncode == 0, (label, text)
                assert 'fixture-secret' not in text
                assert hashes(output) == expected, (label, 'extracted hashes')
                plan = re.search(r'READ_PLAN (\d+) (\d+)', text)
                assert plan and plan[1] == '1', (label, text)
                if solid:
                    assert int(plan[2]) > 1, (label, 'missing solid predecessors', text)
                planned = re.findall(r'READ_RANGE (\d+) (\d+)', text)
                assert len(planned) == int(plan[2])
                assert all(int(start) + int(size) <= len(files[name + '.rar']) for start, size in planned)
                estimates = re.findall(r'ESTIMATE (open|extract|test) (\d+) (\d+)', text)
                assert [x[0] for x in estimates] == ['open', 'extract', 'test'], text
                assert all(x[1] == '1' for x in estimates[1:]), text
                stats = re.findall(r'NETWORK (open|extract|test) (\d+) (\d+) (\d+)', text)
                requests = server.records[first:]
                assert sum(int(x[1]) for x in stats) == sum(r['end'] - r['start'] + 1 for r in requests)
                assert sum(int(x[3]) for x in stats) == len(requests)
                cases.append(dict(label=label, planned_ranges=len(planned), hashes_match=True))
                print('PASS ' + label, flush=True)
    finally:
        server.shutdown()
        server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases,
            fixtures={name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
            requests=server.records), indent=2), encoding='utf-8')
        print('Report: ' + str(work / 'report.json'), flush=True)
    print(f'All {len(cases)} compressed RAR estimate scenarios passed.')


if __name__ == '__main__':
    main()
