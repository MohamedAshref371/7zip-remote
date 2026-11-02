"""Assemble a portable Windows x64 release candidate from a clean source tree."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

from test_remote import ROOT
from release_provenance import source_tree_sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, required=True)
    parser.add_argument('--notes', type=Path, default=ROOT / 'Notes/27-release-candidate.md')
    args = parser.parse_args()
    def git(*command):
        return subprocess.check_output(['git', *command], cwd=ROOT, text=True).strip()
    if git('status', '--porcelain'):
        raise SystemExit('Release packages require a clean Git working tree. Commit source changes first.')
    binary = args.bin_dir.resolve()
    provenance = json.loads((binary / 'build-provenance.json').read_text(encoding='utf-8'))
    if provenance['source_tree_sha256'] != source_tree_sha256():
        raise SystemExit('Release binary source does not match the current source tree. Rebuild first.')
    for name, digest in provenance['binaries'].items():
        if hashlib.sha256((binary / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f'Release binary changed after the build: {name}')
    version = re.search(r'#define REMOTE_ARCHIVE_VERSION "([A-Za-z0-9.+-]+)"',
                        (ROOT / 'CPP/7zip/RemoteVersion.h').read_text())[1]
    assert version.encode('utf-16le') in (binary / '7zFM.exe').read_bytes(), 'Rebuild File Manager after changing the version'
    payload = {'7zRemoteFM.exe': (binary / '7zFM.exe').read_bytes(),
               '7z.exe': (binary / '7z.exe').read_bytes(), '7z.dll': (binary / '7z.dll').read_bytes(),
               'License.txt': (ROOT / 'DOC/License.txt').read_bytes(),
               'unRarLicense.txt': (ROOT / 'DOC/unRarLicense.txt').read_bytes(),
               'RELEASE_NOTES.md': args.notes.read_bytes()}
    for path in sorted((ROOT / 'Lang').iterdir()):
        if path.suffix in ('.txt', '.ttt'):
            payload['Lang/' + path.name] = path.read_bytes()
    payload['README.txt'] = f'''7-Zip Remote Archives {version} (Windows x64 release candidate)

Extract this entire ZIP into one folder. Launch 7zRemoteFM.exe and choose
File > Open URL. Keep 7z.dll beside both executables. No installation is needed. Keep the Lang folder beside 7zRemoteFM.exe.
Choose Tools > Options > Language to change the interface language.
Remote additions currently include Arabic; other translations fall back to English.
This portable copy does not replace your installed 7-Zip. HTTP-enabled builds
store preferences under Software\\7-Zip Remote, separately from upstream 7-Zip.
Existing v0.5.0 preferences are not imported; choose cache settings again.
Changing associations in Tools > Options > System still changes Windows file
associations for this user or machine when you explicitly apply those choices.

The upstream engine is 7-Zip 26.05 by Igor Pavlov and contributors.
The remote extension was developed with substantial contributions from OpenAI Codex.
See License.txt, unRarLicense.txt and RELEASE_NOTES.md.

Direct HTTP(S) archive links and byte-range support are required. Browser logins,
sharing-page resolution, archive modification and multi-volume remote archives
are unsupported. Solid archives may need most of the compressed data. Keep ETag
verification enabled when available. Exact reads can reduce bytes but increase
requests and time. Open URL also offers 32, 64, 128 and 256 KiB download blocks;
256 KiB remains the default. Disk caching reuses downloaded parts subject to verification.

Matching source code is provided in the source ZIP alongside this package.
This package contains no installer, shell extension, test executables or private
test fixtures.
'''.encode('utf-8')
    payload['manifest.json'] = (json.dumps(dict(version=version, platform='windows-x64',
        upstream='7-Zip 26.05', source_commit=git('rev-parse', 'HEAD'),
        source_modified=False, files={name: hashlib.sha256(data).hexdigest()
        for name, data in payload.items()}), indent=2) + '\n').encode('utf-8')
    payload['SHA256SUMS.txt'] = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n'
                                         for name, data in sorted(payload.items())).encode()
    out = ROOT / '.build/releases'
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f'7zip-remote-{version}-windows-x64.zip'
    prefix = f'7zip-remote-{version}'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip:
        for name, data in sorted(payload.items()): zip.writestr(prefix + '/' + name, data)
    with zipfile.ZipFile(archive) as zip:
        assert zip.testzip() is None
        assert len(zip.namelist()) == len(payload)
        for name, data in payload.items(): assert zip.read(prefix + '/' + name) == data
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    source_zip = out / f'7zip-remote-{version}-source.zip'
    source_files = git('ls-files').splitlines()
    with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip:
        for name in source_files:
            path = ROOT / name
            assert path.resolve().is_relative_to(ROOT.resolve()), name
            if path.is_file(): zip.write(path, prefix + '-source/' + name)
    with zipfile.ZipFile(source_zip) as zip:
        assert zip.testzip() is None and len(zip.namelist()) > 100
    source_digest = hashlib.sha256(source_zip.read_bytes()).hexdigest()
    source_zip.with_suffix('.zip.sha256').write_text(f'{source_digest}  {source_zip.name}\n', encoding='ascii')
    print('Verified package:', archive)
    print('SHA256:', digest)
    print('Verified matching source:', source_zip)


if __name__ == '__main__':
    main()
