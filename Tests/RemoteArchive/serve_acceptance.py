"""Disposable loopback-only desktop acceptance fixtures; restore test preferences on exit."""
import io
import json
from pathlib import Path
import random
import subprocess
import tarfile
import tempfile
import time
import winreg
import zipfile

from iso_progress_fixtures import write_iso_tree
from test_remote import FixtureServer, Handler, ROOT


def main():
    work = Path(tempfile.mkdtemp(prefix='desktop-', dir=ROOT / '.build/msvc/runs'))
    inputs = work / 'input'
    (inputs / 'docs').mkdir(parents=True)
    target = b'Desktop acceptance selected file.\n' * 100
    (inputs / 'docs/target.txt').write_bytes(target)
    (inputs / '000-data.bin').write_bytes(random.Random(71).randbytes(4 << 20))
    with zipfile.ZipFile(work / 'sample.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(inputs / '000-data.bin', '000-data.bin')
        z.write(inputs / 'docs/target.txt', 'docs/target.txt')
    with tarfile.open(work / 'scan.tar', 'w') as t:
        for i in range(80):
            name = f'items/file-{i:03}.bin'
            info = tarfile.TarInfo(name); info.size = 300000
            t.addfile(info, io.BytesIO(bytes(300000)))
        info = tarfile.TarInfo('docs/target.txt'); info.size = len(target)
        t.addfile(info, io.BytesIO(target))
    write_iso_tree(work / 'sample.iso', target, joliet=True)
    sevenzip = ROOT / '.build/msvc/next-preview/7z.exe'
    subprocess.run([str(sevenzip), 'a', str(work / 'secret.7z'), '-ms=off', '-mhe=on',
                    '-pacceptance-test', '.'], cwd=inputs, check=True, capture_output=True)
    files = {name: (work / name).read_bytes() for name in ('sample.zip', 'scan.tar', 'sample.iso', 'secret.7z')}
    key_path = r'Software\7-Zip\FM\RemoteCache'
    names = ['Enabled', 'Directory', 'MaxBytes', 'ExactReads']
    before = {}
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            for name in names:
                try: before[name] = winreg.QueryValueEx(key, name)
                except FileNotFoundError: pass
    except FileNotFoundError:
        pass

    class Recording(Handler):
        def do_GET(self):
            if self.path.startswith('/scan/'):
                time.sleep(0.06)
            try: super().do_GET()
            finally:
                with self.server.lock:
                    (work / 'requests.json').write_text(json.dumps(self.server.records, indent=2), encoding='utf-8')

    server = FixtureServer(files)
    server.RequestHandlerClass = Recording
    base = f'http://127.0.0.1:{server.server_port}'
    session = dict(directory=str(work), zip=base + '/ok/sample.zip', iso=base + '/ok/sample.iso',
                   scan=base + '/scan/scan.tar', encrypted=base + '/ok/secret.7z',
                   renewed=base + '/ok/sample.zip?renewed=1', denied=base + '/forbidden/sample.zip',
                   output=str(work / 'output'), cache=str(work / 'cache'), password='acceptance-test')
    (ROOT / '.build/ui-next').mkdir(exist_ok=True)
    (ROOT / '.build/ui-next/session.json').write_text(json.dumps(session, indent=2), encoding='utf-8')
    (work / 'preferences-before.json').write_text(json.dumps(before), encoding='utf-8')
    print(json.dumps(session, indent=2), flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        server.server_close()
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            for name in names:
                if name in before:
                    value, kind = before[name]
                    winreg.SetValueEx(key, name, 0, kind, value)
                else:
                    try: winreg.DeleteValue(key, name)
                    except FileNotFoundError: pass
        print('Test cache/mode preferences restored.', flush=True)


if __name__ == '__main__':
    main()
