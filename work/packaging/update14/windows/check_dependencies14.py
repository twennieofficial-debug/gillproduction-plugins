"""Read-only import audit of the exact staged60 modules and native setup helper."""
from pathlib import Path
import re
import subprocess
from build14 import gate, OUT

SYSTEM={'winmm.dll','imm32.dll','ole32.dll','oleaut32.dll','kernel32.dll','user32.dll','gdi32.dll','shell32.dll','d2d1.dll','dxgi.dll','d3d11.dll','dcomp.dll','comctl32.dll','dwmapi.dll','advapi32.dll','ws2_32.dll','wininet.dll','version.dll','comdlg32.dll','rpcrt4.dll','shlwapi.dll','bcrypt.dll','winhttp.dll','normaliz.dll','uuid.dll','secur32.dll','crypt32.dll','setupapi.dll'}

def audit():
    data=gate.read(OUT/'payload-build.json');production=OUT/'production-build-result.json'
    build=gate.read(production if production.is_file() and gate.read(production)['payload_sha256']==gate.sha(OUT/'payload-build.json') else OUT/'build-result.json')
    gate.require(build['payload_sha256']==gate.sha(OUT/'payload-build.json'),'Build/payload proof differs')
    dumpbin=gate.WORK/'toolchain/msvc/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe'
    files=[(r['path'],Path(data['payload_root'])/'P'/r['path'],r['sha256']) for r in data['files'] if r['root']=='P']
    gate.require(len(files)==60,'Expected60 staged modules')
    files.append(('transaction helper',Path(build['helper']),build['helper_sha256']));rows=[]
    for name,path,fingerprint in files:
        gate.require(gate.sha(path)==fingerprint,'Dependency input changed')
        result=subprocess.run([str(dumpbin),'/DEPENDENTS',str(path)],capture_output=True,text=True,check=True)
        imports=re.findall(r'^\s+([\w.-]+\.dll)\s*$',result.stdout,re.M|re.I)
        gate.require(imports and all(i.lower() in SYSTEM for i in imports),'Unexpected runtime DLL: '+name)
        gate.require(gate.sha(path)==fingerprint,'Dependency input changed during inspection')
        rows.append({'name':name,'sha256':fingerprint,'imports':imports})
    report={'passed':True,'payload_sha256':build['payload_sha256'],'helper_sha256':build['helper_sha256'],'files':rows}
    gate.write(OUT/'dependency-report.json',report);print('60 modules and native helper: only Windows system libraries')
    return report

if __name__=='__main__':audit()
