"""Build Windows x64 binaries using installed Visual Studio and upstream nmake files."""
import argparse
import filecmp
import json
import os
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['engine', 'console', 'filemanager', 'all'], default='all')
    parser.add_argument('--http', action='store_true')
    parser.add_argument('--rebuild', action='store_true', help='Recompile all objects, including header dependents')
    parser.add_argument('--agent-check', action='store_true', help='Link a headless Agent integration check after File Manager')
    parser.add_argument('--bin-dir', type=Path, help='Publish binaries to a separate directory, e.g. while an earlier build is open')
    args = parser.parse_args()
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    instances = json.loads(subprocess.check_output([
        str(vswhere), '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json']))
    if not instances:
        raise SystemExit('Install the Visual Studio Desktop development with C++ workload first.')
    vcvars = Path(instances[0]['installationPath']) / 'VC/Auxiliary/Build/vcvars64.bat'
    out = ROOT / '.build/msvc'
    out.mkdir(parents=True, exist_ok=True)
    binaries = args.bin_dir.resolve() if args.bin_dir else out / ('bin' if args.http else 'bin-local')
    binaries.mkdir(parents=True, exist_ok=True)
    # Batch is only used to obtain the compiler environment, never for file operations.
    setup = out / 'environment.cmd'
    setup.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n')
    result = subprocess.check_output(['cmd', '/d', '/c', str(setup)], text=True)
    env = {key.upper(): value for key, value in os.environ.items()}
    for line in result.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key.upper()] = value
    print('MSVC ' + env.get('VCTOOLSVERSION', '?') + '; SDK ' + env.get('WINDOWSSDKVERSION', '?'), flush=True)
    nmake = shutil.which('nmake.exe', path=env.get('PATH'))
    if not nmake:
        raise SystemExit('nmake was not found in the Visual Studio environment.')
    targets = dict(engine='CPP/7zip/Bundles/Format7zF', console='CPP/7zip/UI/Console',
                   filemanager='CPP/7zip/UI/FileManager')
    selected = targets if args.target == 'all' else [args.target]
    for target in selected:
        obj = out / (target + '-http' if args.http and target != 'engine' else target)
        obj.mkdir(exist_ok=True)
        if ' ' in str(obj):
            raise SystemExit('The upstream makefiles require an output path without spaces.')
        command = [nmake, '/nologo', 'PLATFORM=x64', 'O=' + str(obj)]
        if args.rebuild:
            command += ['/A']
        if args.http and target != 'engine':
            command += ['Z7_HTTP=1']
        log = out / (target + '-build.log')
        print('Building ' + target + '; log: ' + str(log), flush=True)
        with log.open('w') as output:
            build = subprocess.run(command, cwd=ROOT / targets[target], env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
        if build.returncode:
            print('\n'.join(log.read_text(errors='replace').splitlines()[-45:]))
            raise SystemExit(build.returncode)
        name = dict(engine='7z.dll', console='7z.exe', filemanager='7zFM.exe')[target]
        destination = binaries / name
        if not destination.exists() or not filecmp.cmp(obj / name, destination, shallow=False):
            shutil.copy2(obj / name, destination)
        print('Built ' + target, flush=True)
        if target == 'filemanager' and args.agent_check:
            if not args.http:
                raise SystemExit('--agent-check requires --http')
            compiler = shutil.which('cl.exe', path=env.get('PATH'))
            linker = shutil.which('link.exe', path=env.get('PATH'))
            check_obj = out / 'AgentCheck.obj'
            subprocess.run([compiler, '/nologo', '/c', '/MT', '/EHsc', '/std:c++17', '/Gr',
                            '/DZ7_HTTP', '/DZ7_EXTERNAL_CODECS', '/DZ7_LANG', '/DZ7_DEVICE_FILE',
                            str(ROOT / 'Tests/RemoteArchive/AgentCheck.cpp'), '/Fo' + str(check_obj)],
                           cwd=ROOT, env=env, check=True)
            subprocess.run([linker, '/nologo', '/SUBSYSTEM:CONSOLE', '/ENTRY:wmainCRTStartup',
                            '/OPT:REF', '/OPT:ICF', '/OUT:' + str(binaries / 'agentcheck.exe'),
                            str(check_obj), *map(str, obj.glob('*.obj')), str(obj / 'resource.res'),
                            'oleaut32.lib', 'ole32.lib', 'user32.lib', 'advapi32.lib', 'shell32.lib',
                            'comctl32.lib', 'htmlhelp.lib', 'comdlg32.lib', 'mpr.lib', 'gdi32.lib',
                            'delayimp.lib', 'winhttp.lib', '/DELAYLOAD:mpr.dll'], cwd=ROOT, env=env, check=True)


if __name__ == '__main__':
    main()
