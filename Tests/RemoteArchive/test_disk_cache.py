"""Persistent cache identity, corruption, limits, locking and explicit consent."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import _winapi
import ctypes

from test_remote import FixtureServer, ROOT

BLOCK = 256 << 10


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / '.build/remote/cachecheck.exe')
    args = parser.parse_args()
    exe = args.exe.resolve()
    runs = ROOT / '.build/remote/runs'
    runs.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='disk-cache-', dir=runs))
    original = bytes(range(251)) * ((8 << 20) // 251 + 1)
    original = original[:8 << 20]
    server = FixtureServer({'cache.bin': original})
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    cases = []

    def run(label, folder, *, mode='ok', token='a', options=(), code=0, source=None):
        url = source or f'{base}/{mode}/cache.bin?sig=cache-secret-{token}%2F&x=1&x=2'
        result = subprocess.run([str(exe), url, str(folder), *options], capture_output=True, timeout=20)
        text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (work / (label + '.log')).write_text(text, encoding='utf-8')
        assert result.returncode == code, (label, result.returncode, text)
        assert 'cache-secret' not in text, text
        match = re.search(r'DISK (\w+) (\d+) (\d+) (\d+) ([a-f0-9]*)', text)
        stats = dict(result=match[1], requests=int(match[2]), downloaded=int(match[3]), hits=int(match[4]), id=match[5]) if match else {}
        digest = re.search(r'HASH ([a-f0-9]{64})', text)
        if digest and code == 0:
            start = next((int(v.split('=')[1]) for v in options if v.startswith('--cache-start=')), 0) * BLOCK
            count = next((int(v.split('=')[1]) for v in options if v.startswith('--cache-blocks=')), 16) * BLOCK
            assert digest[1] == hashlib.sha256(server.files['cache.bin'][start:start + count]).hexdigest(), (label, text)
        cases.append(dict(label=label, **stats))
        print('PASS ' + label, flush=True)
        return stats, text

    def check_files(folder):
        for path in folder.rglob('*'):
            if path.is_file():
                data = path.read_bytes()
                assert b'cache-secret' not in data and 'cache-secret'.encode('utf-16le') not in data, path
                assert b'http://' not in data and 'http://'.encode('utf-16le') not in data, path

    try:
        run('settings-prepare-new-folder', work / 'prepared', options=('--cache-prepare',))
        run('clear-missing-folder', work / 'missing', options=('--cache-clear',))
        strict = work / 'strict'
        first, _ = run('first-open', strict)
        assert first['hits'] == 0 and first['id']
        second, text = run('verified-reopen', strict)
        assert second['id'] == first['id'] and second['hits'] == 16 and second['downloaded'] == 1
        assert 'Cache verified' in text
        check_files(strict)
        entry = strict / '.7z-http-cache-v1' / first['id']
        temporary = entry / ('b0000000000000000.bin.tmp-' + 'a' * 32)
        temporary.write_bytes(b'incomplete write')
        again, _ = run('incomplete-write-ignored', strict)
        assert again['hits'] == 16 and not temporary.exists()

        # A changed signed query is a different resource until explicitly linked.
        changed, _ = run('new-url-isolated', strict, token='b')
        assert changed['id'] != first['id'] and changed['hits'] == 0
        linked, text = run('manual-url-link', strict, token='c', options=('--cache-trust', '--cache-reuse=' + first['id']))
        assert linked['id'] == first['id'] and linked['hits'] >= 16 and 'partial verification' in text
        untrusted, _ = run('partial-cache-never-upgraded', strict)
        assert untrusted['id'] != first['id'] and untrusted['hits'] == 0

        weak = work / 'no-etag'
        initial, _ = run('no-etag-first', weak, mode='missing-etag', options=('--skip-http-etag',))
        no_reuse, _ = run('bypass-does-not-consent', weak, mode='missing-etag', options=('--skip-http-etag',))
        assert no_reuse['id'] != initial['id'] and no_reuse['hits'] == 0
        accepted, text = run('explicit-no-etag-reuse', weak, mode='missing-etag',
                             options=('--skip-http-etag', '--cache-trust', '--cache-reuse=' + initial['id']))
        assert accepted['id'] == initial['id'] and accepted['hits'] == 16 and 'partial verification' in text
        weak_tag = work / 'weak-tag'
        old, _ = run('bypass-with-known-tag-first', weak_tag, options=('--skip-http-etag',))
        server.etag = '"fixture-v2"'
        fresh, _ = run('known-tag-change-rejects-even-with-consent', weak_tag,
                        options=('--skip-http-etag', '--cache-trust', '--cache-reuse=' + old['id']))
        assert fresh['id'] != old['id'] and fresh['hits'] == 0
        server.etag = '"fixture-v1"'

        late = work / 'late'
        old, _ = run('late-redirect-cache-seed', late, mode='late-cache', options=('--cache-blocks=4',))
        server.redirects['late-cache'] = (307, ['/ok/cache.bin?sig=new-endpoint'])
        server.redirect_after_probe.add('late-cache')
        changed, text = run('late-redirect-never-mixes-cached-scope', late, mode='late-cache', code=1)
        assert changed['hits'] == 4 and not changed['id'] and 'source location changed' in text
        reopened, text = run('late-redirect-reopen-memory-fallback', late, mode='late-cache')
        assert reopened['hits'] == 0 and not reopened['id'] and 'paused after a source redirect' in text

        version = work / 'version'
        initial, _ = run('version-first', version)
        server.etag = '"fixture-v2"'
        fresh, text = run('etag-change-isolated', version)
        assert fresh['id'] != initial['id'] and fresh['hits'] == 0 and 'source changed' in text
        server.etag = '"fixture-v1"'
        server.files['cache.bin'] = original + b'x'
        fresh, _ = run('size-change-isolated', version, options=('--cache-trust', '--cache-reuse=' + initial['id']))
        assert fresh['id'] != initial['id'] and fresh['hits'] == 0
        server.files['cache.bin'] = original

        changed = bytearray(original); changed[8 * BLOCK + 50] ^= 31
        server.files['cache.bin'] = bytes(changed)
        rejected, _ = run('strict-cache-rebind-rejected', version, token='replacement',
                          options=('--cache-trust', '--cache-reuse=' + initial['id']))
        assert rejected['id'] and rejected['id'] != initial['id']
        server.files['cache.bin'] = original
        preserved, _ = run('rejected-bind-preserves-old-trust', version)
        assert preserved['id'] == initial['id'] and preserved['hits'] == 16

        partial = work / 'samples'
        initial, _ = run('sample-first', partial, mode='missing-etag', options=('--skip-http-etag',))
        modified = bytearray(original)
        modified[8 * BLOCK + 50] ^= 17 # middle of sixteen stored blocks
        server.files['cache.bin'] = bytes(modified)
        fresh, text = run('sample-change-isolated', partial, mode='missing-etag', token='new',
                          options=('--skip-http-etag', '--cache-trust', '--cache-reuse=' + initial['id']))
        assert fresh['id'] != initial['id'] and fresh['hits'] == 0 and 'source changed' in text
        server.files['cache.bin'] = original

        damaged = work / 'damaged'
        saved, _ = run('corrupt-first', damaged)
        entry = damaged / '.7z-http-cache-v1' / saved['id']
        block = entry / 'b0000000000000000.bin'
        data = bytearray(block.read_bytes()); data[-10] ^= 31; block.write_bytes(data)
        repaired, text = run('corrupt-block-refetched', damaged)
        assert repaired['id'] == saved['id'] and repaired['hits'] == 15 and 'Damaged cache block' in text
        info = entry / 'info.bin'
        data = bytearray(info.read_bytes()); data[30] ^= 63; info.write_bytes(data)
        repaired, _ = run('corrupt-manifest-isolated', damaged)
        assert repaired['id'] != saved['id'] and repaired['hits'] == 0
        run('damaged-cache-clear', damaged, options=('--cache-clear',))
        assert not list((damaged / '.7z-http-cache-v1').glob('*/b*.bin'))

        mixed = work / 'mixed-parts'
        a, _ = run('part-binding-first-version', mixed)
        modified = bytearray(original); modified[50] ^= 7
        server.files['cache.bin'] = bytes(modified); server.etag = '"fixture-v2"'
        b, _ = run('part-binding-second-version', mixed)
        root = mixed / '.7z-http-cache-v1'
        name = 'b0000000000000000.bin'
        (root / a['id'] / name).write_bytes((root / b['id'] / name).read_bytes())
        server.files['cache.bin'] = original; server.etag = '"fixture-v1"'
        recovered, text = run('foreign-part-never-published', mixed)
        assert recovered['id'] == a['id'] and recovered['hits'] == 15 and 'Damaged cache block' in text

        failed_io = work / 'write-failure'
        saved, _ = run('write-failure-first', failed_io)
        block = failed_io / '.7z-http-cache-v1' / saved['id'] / 'b0000000000000000.bin'
        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        api.CreateFileW.restype = ctypes.c_void_p
        api.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = api.CreateFileW(str(block), 0x80000000, 0, None, 3, 0, None)
        assert handle not in (None, ctypes.c_void_p(-1).value)
        try:
            fallback, text = run('read-write-failure-keeps-extraction', failed_io)
            assert fallback['hits'] == 15 and 'Cache write failed' in text
            run('clear-io-failure-retains-ownership', failed_io, options=('--cache-clear',), code=1)
            assert (block.parent / 'info.bin').exists() and (block.parent / 'entry.owner').exists()
        finally:
            api.CloseHandle(handle)
        run('clear-after-io-recovers', failed_io, options=('--cache-clear',))
        assert not block.exists() and not list(failed_io.rglob('*.tmp-*'))

        limited = work / 'limited'
        saved, _ = run('unlimited-cache', limited, options=('--cache-limit=0',))
        limited_stats, _ = run('lowered-limit-on-open', limited, options=('--cache-limit=1048576',))
        used = sum(p.stat().st_size for p in (limited / '.7z-http-cache-v1').rglob('*') if p.is_file())
        assert used <= 1048576 and limited_stats['id'] == saved['id']

        bad = work / 'not-a-folder'; bad.write_bytes(b'preserve')
        stats, text = run('io-failure-memory-fallback', bad)
        assert not stats['id'] and stats['hits'] == 0 and bad.read_bytes() == b'preserve'
        foreign = work / 'foreign'; (foreign / '.7z-http-cache-v1').mkdir(parents=True)
        sentinel = foreign / '.7z-http-cache-v1/unrelated.txt'; sentinel.write_bytes(b'keep me')
        stats, _ = run('unowned-folder-not-adopted', foreign)
        assert not stats['id'] and sentinel.read_bytes() == b'keep me'
        run('unowned-folder-not-cleared', foreign, options=('--cache-clear',), code=1)
        assert sentinel.read_bytes() == b'keep me'

        external = work / 'external'; external.mkdir()
        sentinel = external / 'unrelated.txt'; sentinel.write_bytes(b'preserve junction target')
        run('junction-target-prepare', external, options=('--cache-prepare',))
        link = work / 'cache-root-link'
        _winapi.CreateJunction(str(external), str(link))
        stats, _ = run('junction-root-memory-fallback', link)
        assert not stats['id'] and not list((external / '.7z-http-cache-v1').glob('*/b*.bin'))
        run('junction-root-not-cleared', link, options=('--cache-clear',), code=1)
        assert sentinel.read_bytes() == b'preserve junction target'
        safe_root = work / 'junction-child'
        run('junction-child-prepare', safe_root, options=('--cache-prepare',))
        child = safe_root / '.7z-http-cache-v1' / ('f' * 32)
        _winapi.CreateJunction(str(external), str(child))
        run('junction-child-not-followed', safe_root, options=('--cache-clear',))
        assert child.exists() and sentinel.read_bytes() == b'preserve junction target'

        concurrent = work / 'concurrent'
        saved, _ = run('concurrent-first', concurrent)
        process = subprocess.Popen([str(exe), f'{base}/ok/cache.bin?sig=cache-secret-a%2F&x=1&x=2', str(concurrent),
                                    '--cache-hold'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            ready = threading.Event()
            lines = []
            def read_lines():
                for line in process.stdout:
                    lines.append(line)
                    if line.strip() == 'CACHE_READY': ready.set()
            reader = threading.Thread(target=read_lines, daemon=True); reader.start()
            assert ready.wait(10), lines
            other, text = run('locked-entry-memory-fallback', concurrent)
            assert not other['id'] and 'busy' in text
            old_blocks = {p.name: p.read_bytes() for p in (concurrent / '.7z-http-cache-v1' / saved['id']).glob('b*.bin')}
            _, text = run('limit-retains-foreign-active-data', concurrent, token='other', options=('--cache-limit=1048576',))
            assert 'active entries retained' in text
            assert all((concurrent / '.7z-http-cache-v1' / saved['id'] / name).read_bytes() == data
                       for name, data in old_blocks.items())
            _, text = run('active-clear-skipped', concurrent, options=('--cache-clear',))
            assert 'CLEARED 1 1' in text
            # Terminate our own checker without destructors: committed blocks
            # must remain reusable and the OS must release its cache lock.
            process.kill()
            assert process.wait(timeout=3) != 0
            reader.join(1)
        finally:
            if process.poll() is None: process.kill(); process.wait()
        retained, _ = run('crash-reopen-retains-complete-data', concurrent)
        assert retained['id'] == saved['id'] and retained['hits'] == 16
        sentinel = concurrent / 'unrelated.txt'; sentinel.write_bytes(b'preserve root files')
        nested = concurrent / '.7z-http-cache-v1' / saved['id'] / 'my-notes.txt'
        nested.write_bytes(b'preserve unexpected entry files')
        run('inactive-clear', concurrent, options=('--cache-clear',))
        assert sentinel.read_bytes() == b'preserve root files'
        assert nested.read_bytes() == b'preserve unexpected entry files'
        assert not list((concurrent / '.7z-http-cache-v1').glob('*/b*.bin'))

        cancel_dir = work / 'cancel'
        first_request = len(server.records)
        stats, _ = run('cancel-during-validation', cancel_dir, mode='cache-slow', options=('--cache-cancel-ms=200',), code=3)
        assert stats['result'] == '80004004' and not list(cancel_dir.rglob('b*.bin'))
        assert [(r['start'], r['end']) for r in server.records[first_request:]] == [(0, 0), (0, 65535)]
    finally:
        server.release_holds.set(); server.shutdown(); server.server_close()
        (work / 'report.json').write_text(json.dumps(dict(cases=cases), indent=2), encoding='utf-8')
    print(f'All {len(cases)} disk cache scenarios passed. Report: {work / "report.json"}')


if __name__ == '__main__':
    main()
