"""Validate shipped Lang files, stable remote IDs, translations and upstream provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from test_remote import ROOT


def parse_lang(path):
    # Same sequential-ID and escape conventions as CPP/Common/Lang.cpp.
    text = path.read_bytes().decode('utf-8-sig').replace('\r', '')
    assert text.startswith(';!@Lang2@!UTF-8!\n'), path
    entries = {}
    current = -1024
    for line in text.split('\n')[1:]:
        if not line.strip() or line.startswith(';'):
            current += 1
            continue
        if re.fullmatch(r'[0-9]+', line):
            value = int(line)
            assert current <= value <= (1 << 30), (path, value, current)
            current = value
            continue
        assert current >= 0 and current not in entries, (path, current)
        entries[current] = re.sub(r'\\([nt\\])', lambda m: {'n': '\n', 't': '\t', '\\': '\\'}[m[1]], line)
        current += 1
    assert entries[0] == '7-Zip', path
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent', type=Path, help='Run the real CLang / RemoteLang checks on Windows')
    args = parser.parse_args()
    upstream = json.loads((ROOT / 'Tests/RemoteArchive/lang_upstream.json').read_text())
    paths = sorted((ROOT / 'Lang').iterdir())
    assert {p.name for p in paths} == set(upstream['files']), 'Missing or unexpected language files'
    assert len(paths) == 93 and sum(p.suffix == '.txt' for p in paths) == 92
    parsed = {p.name: parse_lang(p) for p in paths}
    for p in paths:
        if p.name not in ('ar.txt', 'en.ttt'):
            assert hashlib.sha256(p.read_bytes()).hexdigest() == upstream['files'][p.name], p
        else:
            original = p.read_bytes().split(b'\r\n; 7-Zip Remote extension', 1)[0]
            assert hashlib.sha256(original).hexdigest() == upstream['files'][p.name], p
    catalog = (ROOT / 'CPP/7zip/UI/FileManager/RemoteLangStrings.inc').read_text()
    entries = [(int(key), json.loads(text)) for key, text in
               re.findall(r'^REMOTE_LANG\((\d+), L("(?:[^"\\]|\\.)*")\)$', catalog, re.M)]
    assert len(entries) >= 134 and len({key for key, _ in entries}) == len(entries)
    assert len({text for _, text in entries}) == len(entries), 'Ambiguous English keys'
    for key, english in entries:
        assert key >= 900000 and parsed['en.ttt'][key] == english, key
        arabic = parsed['ar.txt'][key]
        assert re.search(r'[\u0600-\u06ff]', arabic), (key, arabic)
        assert sorted(re.findall(r'\{\d+\}', arabic)) == sorted(re.findall(r'\{\d+\}', english)), key
    remote_keys = {key for key, _ in entries}
    assert {key for key in parsed['ar.txt'] if key >= 900000} == remote_keys
    assert {key for key in parsed['en.ttt'] if key >= 900000} == remote_keys
    for name, values in parsed.items():
        if name not in ('ar.txt', 'en.ttt'):
            assert not remote_keys.intersection(values), name
    if args.agent:
        subprocess.run([str(args.agent.resolve()), '--language-check', *map(str, paths)],
                       cwd=ROOT, check=True)
    print(f'PASS {len(paths)} language files; {len(entries)} Arabic/English remote strings; upstream bytes and placeholders preserved.')


if __name__ == '__main__':
    main()
