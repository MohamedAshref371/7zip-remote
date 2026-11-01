"""Hash build inputs so a portable binary cannot be paired with different code."""
import hashlib
from pathlib import Path
import subprocess

from test_remote import ROOT


def source_tree_sha256(root=ROOT):
    names = subprocess.check_output(
        ['git', '-c', 'core.quotepath=false', 'ls-files', '--cached', '--others',
         '--exclude-standard', '-z', '--', 'C', 'CPP', 'Asm'], cwd=root).split(b'\0')
    digest = hashlib.sha256()
    for raw in sorted(name for name in names if name):
        path = Path(raw.decode('utf-8'))
        content = (root / path).read_bytes()
        digest.update(len(raw).to_bytes(4, 'little'))
        digest.update(raw)
        digest.update(len(content).to_bytes(8, 'little'))
        digest.update(content)
    return digest.hexdigest()
