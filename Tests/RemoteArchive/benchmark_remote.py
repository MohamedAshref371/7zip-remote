"""Measure downloaded bytes, requests, wall time and cache files on controlled archives."""
import argparse
import io
import json
from pathlib import Path
import random
import re
import subprocess
import tarfile
import tempfile
import threading
import time
import zipfile

from estimate_fixtures import write_rar5
from test_remote import FixtureServer, Handler, ROOT


class DelayedHandler(Handler):
    def do_GET(self):
        time.sleep(self.server.delay_ms / 1000)
        super().do_GET()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--files', type=int, default=256)
    parser.add_argument('--latency-ms', type=int, nargs='+', default=[0, 20])
    args = parser.parse_args()
    assert 1 <= args.files <= 10000 and all(0 <= n <= 1000 for n in args.latency_ms)
    root = ROOT / '.build/benchmarks'
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='remote-', dir=root))
    exe = args.exe.resolve()
    rng = random.Random(504)
    target = b'benchmark selected file\n' * 100
    entries = [(f'items/item-{i:05}.bin', rng.randbytes(4096)) for i in range(args.files)]
    entries.append(('docs/target.txt', target))
    inputs = work / 'input'
    for name, data in entries:
        path = inputs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    with zipfile.ZipFile(work / 'sample.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries: archive.writestr(name, data)
    with tarfile.open(work / 'sample.tar', 'w', format=tarfile.USTAR_FORMAT) as archive:
        for name, data in entries:
            item = tarfile.TarInfo(name); item.size = len(data)
            archive.addfile(item, io.BytesIO(data))
    write_rar5(work / 'sample.rar', entries)
    for name, solid in [('nonsolid.7z', 'off'), ('solid.7z', 'on')]:
        subprocess.run([str(exe.with_name('7z.exe')), 'a', str(work / name), '-ms=' + solid, '.'],
                       cwd=inputs, capture_output=True, check=True)
    files = {p.name: p.read_bytes() for p in work.iterdir() if p.suffix in ('.zip', '.tar', '.rar', '.7z')}
    server = FixtureServer(files)
    server.RequestHandlerClass = DelayedHandler
    threading.Thread(target=server.serve_forever, daemon=True).start()
    rows = []
    try:
        for delay in args.latency_ms:
            server.delay_ms = delay
            for name in files:
                for mode in ('blocks', 'exact', 'exact-disk-cold', 'exact-disk-warm'):
                    output = work / f'{delay}-{name}-{mode}'
                    cache = work / f'cache-{delay}-{name}'
                    options = [] if mode == 'blocks' else ['--exact-http-reads']
                    if 'disk' in mode: options.append('--cache-dir=' + str(cache))
                    before = len(server.records)
                    start = time.perf_counter()
                    result = subprocess.run([str(exe), f'http://127.0.0.1:{server.server_port}/ok/{name}',
                                             str(output), *options], capture_output=True, timeout=300)
                    seconds = time.perf_counter() - start
                    log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                    (work / f'{delay}-{name}-{mode}.log').write_text(log, encoding='utf-8')
                    assert result.returncode == 0, (name, mode, log)
                    assert (output / 'target.txt').read_bytes() == target
                    requests = server.records[before:]
                    stages = list(map(int, re.findall(r'^STAGE (\d+)', log, re.MULTILINE)))
                    assert stages == ([0, 1, 2, 3, 4] if 'disk' in mode else [0, 2, 3, 4]), stages
                    saved = [p for p in cache.rglob('*') if p.is_file()] if 'disk' in mode else []
                    row = dict(archive=name, files=args.files + 1, latency_ms=delay, mode=mode,
                               bytes=sum(r['end'] - r['start'] + 1 for r in requests),
                               requests=len(requests), seconds=round(seconds, 3),
                               cache_files=len(saved), cache_bytes=sum(p.stat().st_size for p in saved))
                    rows.append(row)
                    print(json.dumps(row), flush=True)
    finally:
        server.shutdown(); server.server_close()
        report = dict(note='Controlled loopback server; added delay per request, not a full internet simulation. One timing sample per cell. All selected outputs verified.', rows=rows)
        (work / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        table = ['# Remote archive benchmark', '', report['note'], '',
                 '| Archive | Delay/request (ms) | Mode | Body bytes | Requests | Seconds | Cache files |',
                 '|---|---:|---|---:|---:|---:|---:|']
        table += [f"| {r['archive']} | {r['latency_ms']} | {r['mode']} | {r['bytes']:,} | {r['requests']} | {r['seconds']} | {r['cache_files']} |" for r in rows]
        (work / 'report.md').write_text('\n'.join(table) + '\n', encoding='utf-8')
    print('Report:', work / 'report.md', flush=True)


if __name__ == '__main__':
    main()
