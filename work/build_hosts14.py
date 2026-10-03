from pathlib import Path
import subprocess

OUT=Path('E:/GILLPRODUCTION/Development14')
for name,relative in [('QualityHost','GILLCommon/Tests/QualityHostCMake'),('MixRealHost','GILLMIX/Tests/HostCMake')]:
    folder=OUT/name
    folder.mkdir(exist_ok=True)
    script=folder/'build.cmd'
    script.write_text('@echo off\ncall S:\\work\\toolchain\\msvc\\setup_x64.bat\nset "PATH=S:\\work\\toolchain\\ninja;S:\\work\\toolchain\\cmake\\cmake\\data\\bin;%PATH%"\n'
       +f'cmake -S S:/work/{relative} -B {folder.as_posix()} -G Ninja -DCMAKE_BUILD_TYPE=Release -DGILL_JUCE_EXPORT=S:/work/juce-export/JUCEExportConfig.cmake -DGILL_PREBUILT_RUNTIME=S:/work/GILLRESTORATION/build-msvc/GillRestorationRuntime.lib\nif errorlevel 1 exit /b %errorlevel%\ncmake --build {folder.as_posix()} --parallel 2\nexit /b %errorlevel%\n')
    print('BUILD '+name,flush=True)
    with (folder/'build.log').open('wb') as log:
        result=subprocess.run(['cmd','/d','/c',str(script)],stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:
        print((folder/'build.log').read_text(errors='replace')[-5000:],flush=True)
        raise SystemExit(result.returncode)
    print(name+' compiled',flush=True)
