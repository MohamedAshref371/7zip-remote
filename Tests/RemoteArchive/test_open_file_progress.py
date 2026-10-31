"""Names reported while TAR/RAR headers are read, without extra HTTP reads."""
import argparse
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import threading

from estimate_fixtures import write_rar4, write_rar5
from test_remote import FixtureServer, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='open-file-progress-', dir=ROOT / '.build/msvc/runs'))
    target = b'selected file\n' * 100
    entries = [(f'folder/file-{i:03}.bin', bytes([i]) * 300000) for i in range(12)]
    entries.append(('docs/target.txt', target))
    files = {}
    for name, writer in [('rar4.rar', write_rar4), ('rar5.rar', write_rar5)]:
        writer(work / name, entries)
        files[name] = (work / name).read_bytes()
    tar_entries = [*entries[:-1], ('docs/\u0645\u0644\u0641.txt', b'unicode'), entries[-1]]
    with tarfile.open(work / 'sample.tar', 'w', format=tarfile.PAX_FORMAT) as tar:
        for name, data in tar_entries:
            item = tarfile.TarInfo(name)
            item.size = len(data)
            tar.addfile(item, io.BytesIO(data))
    files['sample.tar'] = (work / 'sample.tar').read_bytes()
    server = FixtureServer(files)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, name, options=(), local=False, show=True, cancel=False):
        output = work / label
        source = str(work / name) if local else f'{base}/ok/{name}'
        before = len(server.records)
        result = subprocess.run([str(exe), source, str(output), *options,
                                 *(['--open-file-progress'] if show else [])],
                                capture_output=True, timeout=90)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(log, encoding='utf-8')
        assert result.returncode == (3 if cancel else 0), (label, log)
        events = [(int(count), path.replace('\\', '/')) for count, path in
                  re.findall(r'^OPEN_FILE (\d+) ([^\r\n]*)', log, re.MULTILINE)]
        expected = tar_entries if name.endswith('.tar') else entries
        if show:
            if cancel:
                expected = expected[:5]
            assert events == [(i + 1, n) for i, (n, _) in enumerate(expected)], (label, events)
        else:
            assert not events
        if not cancel:
            assert (output / 'target.txt').read_bytes() == target
        requests = [(r['start'], r['end']) for r in server.records[before:]]
        cases.append(dict(label=label, files=len(events), requests=len(requests)))
        print(f'PASS {label}: {len(events)} names, {len(requests)} requests', flush=True)
        return requests

    try:
        for name in files:
            run(name + '-local', name, local=True)
            for exact in (False, True):
                options = ['--exact-http-reads'] if exact else []
                suffix = '-exact' if exact else '-blocks'
                hidden = run(name + suffix + '-hidden', name, options, show=False)
                shown = run(name + suffix + '-shown', name, options)
                assert hidden == shown, (name, hidden, shown)
                run(name + suffix + '-cancel', name, [*options, '--cancel-open-file=5'], cancel=True)
            cache = work / ('cache-' + name)
            options = ['--exact-http-reads', '--cache-dir=' + str(cache)]
            run(name + '-cache-first', name, options)
            assert run(name + '-cache-reopen', name, options) == [(0, 0)]
    finally:
        server.shutdown()
        server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} open-file progress scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
