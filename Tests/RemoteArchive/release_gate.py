"""Build and run the Windows x64 release-candidate checks from one command.

Install Zig 0.13.0 into .build/tools before --build (see the test guide).
Packaging requires a clean commit; run --package after committing the checked source.
"""
import argparse
from pathlib import Path
import subprocess
import sys

from test_remote import ROOT


def run(*args):
    command = [sys.executable, str(ROOT / 'Tests/RemoteArchive' / args[0]), *map(str, args[1:])]
    print('RUN', ' '.join(command[1:]), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / '.build/msvc/release-candidate')
    parser.add_argument('--build', action='store_true', help='Rebuild HTTP and local-only binaries and the cache checker')
    parser.add_argument('--package', action='store_true', help='Package and verify a clean committed source tree')
    parser.add_argument('--legacy-agent', type=Path, help='Previous-version Agent for legacy cache migration checks')
    args = parser.parse_args()
    run('test_localization.py')
    binary = args.bin_dir.resolve()
    local = binary.with_name(binary.name + '-local')
    if args.build:
        run('build_msvc.py', '--http', '--rebuild', '--agent-check', '--bin-dir', binary)
        run('build_msvc.py', '--bin-dir', local)
        run('build.py', '--cache-check')
    console, agent, manager = (binary / name for name in ('7z.exe', 'agentcheck.exe', '7zFM.exe'))
    checker = ROOT / '.build/remote/cachecheck.exe'
    for path in (console, agent, manager, binary / '7z.dll', checker):
        if not path.is_file():
            raise SystemExit(f'Missing {path}. Run with --build first.')
    suites = [
        ('test_localization.py', '--agent', agent),
        ('test_cli.py', '--exe', console),
        ('test_agent.py', '--exe', agent),
        ('test_redirect.py', '--exe', console, '--agent', agent),
        ('test_retry.py', '--exe', console, '--agent', agent),
        ('test_cancel.py', '--exe', agent),
        ('test_exact_reads.py', '--exe', agent),
        ('test_block_sizes.py', '--exe', agent),
        ('test_disk_cache.py', '--exe', checker),
        ('test_disk_cache_agent.py', '--exe', agent, '--checker', checker),
        ('test_disk_cache_agent.py', '--exe', agent, '--checker', checker, '--exact-http-reads'),
        ('test_renewal.py', '--exe', agent),
        ('test_renewal.py', '--exe', agent, '--exact-http-reads'),
        ('test_open_file_progress.py', '--exe', agent),
        ('test_cache_layout.py', '--exe', manager),
        ('test_progress_layout.py', '--exe', manager),
    ]
    packed = ('test_packed_cache.py', '--exe', checker)
    suites.append((*packed, '--legacy-agent', args.legacy_agent.resolve()) if args.legacy_agent
                  else (*packed, '--skip-legacy'))
    for suite in suites:
        run(*suite)
    if local.joinpath('7zFM.exe').is_file():
        run('test_cache_layout.py', '--exe', manager, '--local-exe', local / '7zFM.exe')
        run('test_progress_layout.py', '--exe', manager, '--local-exe', local / '7zFM.exe')
    if args.package:
        run('package_release.py', '--bin-dir', binary)
        run('verify_release.py')
    print('Release candidate gate passed.', flush=True)


if __name__ == '__main__':
    main()
