"""Verify both release ZIPs, their hashes, source provenance and extracted CLI."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

from test_remote import ROOT


def main():
    version = re.search(r'#define REMOTE_ARCHIVE_VERSION "([A-Za-z0-9.+-]+)"',
                        (ROOT / 'CPP/7zip/RemoteVersion.h').read_text())[1]
    release = ROOT / '.build/releases'
    stem = f'7zip-remote-{version}'
    binary = release / f'{stem}-windows-x64.zip'
    source = release / f'{stem}-source.zip'
    for archive in (binary, source):
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        sidecar = archive.with_suffix('.zip.sha256').read_text(encoding='ascii').strip()
        assert sidecar == f'{digest}  {archive.name}', archive
    with zipfile.ZipFile(source) as archive:
        assert archive.testzip() is None
        tracked = subprocess.check_output(['git', 'ls-files'], cwd=ROOT, text=True).splitlines()
        expected = {f'{stem}-source/{name}' for name in tracked}
        assert set(archive.namelist()) == expected
        for name in tracked:
            assert archive.read(f'{stem}-source/{name}') == (ROOT / name).read_bytes(), name
    with zipfile.ZipFile(binary) as archive, tempfile.TemporaryDirectory(prefix='release-verify-', dir=release) as temp:
        assert archive.testzip() is None
        prefix = stem + '/'
        names = set(archive.namelist())
        manifest = json.loads(archive.read(prefix + 'manifest.json'))
        assert manifest['version'] == version and manifest['source_modified'] is False
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        assert manifest['source_commit'] == head
        assert names == {prefix + name for name in (*manifest['files'], 'manifest.json', 'SHA256SUMS.txt')}
        sums = archive.read(prefix + 'SHA256SUMS.txt').decode('ascii').splitlines()
        expected_sums = [f'{digest}  {name}' for name, digest in sorted(manifest['files'].items())]
        expected_sums.append(f'{hashlib.sha256(archive.read(prefix + "manifest.json")).hexdigest()}  manifest.json')
        assert sorted(sums) == sorted(expected_sums)
        for name, digest in manifest['files'].items():
            assert hashlib.sha256(archive.read(prefix + name)).hexdigest() == digest, name
        language_files = {path.name: path.read_bytes() for path in (ROOT / 'Lang').iterdir()
                          if path.suffix in ('.txt', '.ttt')}
        assert {name for name in manifest['files'] if name.startswith('Lang/')} == {
            'Lang/' + name for name in language_files}
        for name, data in language_files.items():
            assert archive.read(prefix + 'Lang/' + name) == data, name
        archive.extractall(temp)
        exe = Path(temp) / stem / '7z.exe'
        subprocess.run([sys.executable, str(ROOT / 'Tests/RemoteArchive/test_cli.py'), '--exe', str(exe)],
                       cwd=ROOT, check=True)
    print('Release ZIPs, source tree, checksums, provenance and extracted CLI verified.')


if __name__ == '__main__':
    main()
