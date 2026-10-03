"""Native Windows builds for the new release, with per-group source receipts."""
from pathlib import Path
import subprocess, json, sys, hashlib
from datetime import datetime, timezone

W = Path(__file__).resolve().parent
OUT = Path('E:/GILLPRODUCTION/Development14')
OUT.mkdir(parents=True, exist_ok=True)

def snapshot(group):
    files = [W/group/'CMakeLists.txt']
    for folder in ('Source','Assets','ThirdParty'):
        files += [p for p in (W/group/folder).rglob('*') if p.is_file()]
    files += [p for p in (W/group/'Tests').rglob('*') if p.is_file() and (p.suffix in ('.cpp','.h','.hpp','.mm') or p.name=='CMakeLists.txt') and not any(x.startswith('build') for x in p.parts)]
    files += [p for p in (W/'GILLCommon').rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.cpp','.mm','.png','.txt')]
    return {p.relative_to(W).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(files))}

catalog = json.loads((W/'packaging/macos/products.json').read_text(encoding='utf-8'))
groups = sys.argv[1:] or sorted({p['group'] for p in catalog})
for group in groups:
    before = snapshot(group)
    started = datetime.now(timezone.utc).isoformat()
    build = OUT/group.removeprefix('GILL')
    build.mkdir(exist_ok=True)
    script = build/'build14.cmd'
    script.write_text('@echo off\ncall S:\\work\\toolchain\\msvc\\setup_x64.bat\nset "PATH=S:\\work\\toolchain\\ninja;S:\\work\\toolchain\\cmake\\cmake\\data\\bin;%PATH%"\n'
      + f'cmake -S S:/work/{group} -B {build.as_posix()} -G Ninja -DCMAKE_BUILD_TYPE=Release -DGILL_JUCE_EXPORT=S:/work/juce-export/JUCEExportConfig.cmake -DGILL_PREBUILT_RUNTIME=S:/work/GILLRESTORATION/build-msvc/GillRestorationRuntime.lib\nif errorlevel 1 exit /b %errorlevel%\ncmake --build {build.as_posix()} --parallel 2\nexit /b %errorlevel%\n')
    print('BUILD '+group, flush=True)
    with (build/'build14.log').open('w') as f:
        code = subprocess.call(['cmd','/d','/c',str(script)], stdout=f, stderr=subprocess.STDOUT)
    result = dict(group=group, build_exit=code, started_at=started)
    if code:
        print((build/'build14.log').read_text(errors='replace')[-7000:],flush=True)
    else:
        print('TEST '+group,flush=True)
        with (build/'ctest14.log').open('w') as f:
            result['test_exit'] = subprocess.call(['S:/work/toolchain/cmake/cmake/data/bin/ctest.exe','--test-dir',str(build),'--output-on-failure','--output-junit',str(build/'ctest14.xml')], stdout=f, stderr=subprocess.STDOUT)
        print((build/'ctest14.log').read_text(errors='replace')[-3000:],flush=True)
    after = snapshot(group)
    result.update(source_files=before, source_unchanged=before==after, completed_at=datetime.now(timezone.utc).isoformat())
    if before != after:
        result['test_exit'] = 1
        result['error'] = 'Group/shared source changed during the build; rebuild required'
    (build/'build14.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(group+' '+('PASS' if code==0 and result.get('test_exit')==0 else 'FAIL'),flush=True)
    if code or result.get('test_exit',1):
        sys.exit(1)
