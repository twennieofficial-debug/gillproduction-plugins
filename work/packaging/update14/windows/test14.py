"""Fresh isolated57->60 installer test, reusing proven13 transaction semantics."""
from pathlib import Path
import ctypes, json, os, shutil, subprocess, time, winreg
import gate14 as gate
from build14 import OUT, TEST_ROOT, recipe_hashes


def snapshot(root):
    return {p.relative_to(root).as_posix():(gate.sha(p),p.stat().st_mtime_ns,p.stat().st_ino) for p in root.rglob('*') if p.is_file()}

def registry():
    try:key=winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'Software\Microsoft\Windows\CurrentVersion\Uninstall\GILLPRODUCTION.VST3',0,winreg.KEY_READ|winreg.KEY_WOW64_64KEY)
    except FileNotFoundError:return None
    with key:return sorted(winreg.EnumValue(key,i) for i in range(winreg.QueryInfoKey(key)[1]))

def main():
    validated=gate.verified_manifest();build=gate.read(OUT/'build-result.json');data=gate.read(OUT/'payload-build.json')
    gate.require(build.get('production') is False and build.get('development_only') is False,'Only validated TEST binaries may run')
    gate.require(build['validated_manifest_sha256']==data['validated_manifest_sha256']==gate.sha(gate.MANIFEST)
                 and gate.sha(OUT/'payload-build.json')==build['payload_sha256'],'Test payload binding changed')
    for key in ('helper','installer'):gate.require(gate.sha(build[key])==build[key+'_sha256'],'Test artifact changed')
    recipe_before=recipe_hashes();gate.require(recipe_before==build['recipe_sha256'],'Test recipes changed since build')
    helper=Path(build['helper']);installer=Path(build['installer']);payload=Path(data['payload_root']);rows=data['files']
    for row in rows:gate.require(gate.sha(payload/row['root']/row['path'])==row['sha256'],'Staged payload changed')
    old,_=gate.baseline_check();baseline={r['name']:r for r in old['products']}
    prefix='run'+str(time.time_ns());case=prefix+'-upgrade13';root=TEST_ROOT/case
    gate.require(not root.exists(),'Isolated case must be new')
    checks=[];env=dict(os.environ,TEMP=str(gate.BASE/'Temp'),TMP=str(gate.BASE/'Temp'))
    live=Path('C:/Program Files/Common Files/VST3/GILLPRODUCTION');before_live=snapshot(live);before_reg=registry()
    def check(label,ok,**extra):
        checks.append({'test':label,'passed':bool(ok),**extra});print(('PASS ' if ok else 'FAIL ')+label,flush=True)
        gate.require(ok,label)
    def target(row):return root/('Plugins' if row['root']=='P' else 'Support')/row['path']
    def run(*args,nsis=False):
        command=[str(installer),'/S','/CASE='+case] if nsis else [str(helper),'--case',case,'--payload',str(payload),'--uninstaller',str(helper),*args]
        result=subprocess.run(command,capture_output=True,timeout=300,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        (OUT/(prefix+'-'+str(len(checks))+'.log')).write_bytes(result.stdout+result.stderr)
        return result.returncode
    for row in rows:
        name=Path(row['path']).stem
        if row['root']=='P' and name in baseline:
            dest=target(row);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(baseline[name]['module'],dest)
            gate.require(gate.sha(dest)==baseline[name]['sha256'],'Seed bytes differ')
    note=root/'Plugins/USER-NOTES.txt';note.write_text('keep user file',encoding='utf-8')
    before=snapshot(root)
    check('transaction semantics match previously tested13',gate.transaction_reuse()['only_release_identifiers_changed'])
    check('old57 factory identities preserved',validated['quality_matrix']['old57_factory_ids_preserved'])
    old_row=next(r for r in rows if r['root']=='P' and Path(r['path']).stem in baseline)
    mapped=ctypes.WinDLL(str(target(old_row)))
    api=ctypes.WinDLL('kernel32',use_last_error=True);api.FreeLibrary.argtypes=[ctypes.c_void_p];api.FreeLibrary.restype=ctypes.c_int
    try:check('loaded old module refuses before writes',run()==20 and snapshot(root)==before)
    finally:gate.require(api.FreeLibrary(mapped._handle)!=0,'Cannot release test DLL')
    fixture_dir=OUT/(prefix+'-process-fixture');fixture_dir.mkdir()
    fixture=fixture_dir/'FL64.exe';shutil.copy2(helper,fixture)
    process=subprocess.Popen([str(fixture),'--wait-fl-fixture'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(.2)
        check('FL process gate refuses before writes',process.poll() is None and run('--require-fl-closed')==28 and snapshot(root)==before)
    finally:
        process.terminate();process.wait(timeout=10)
    changed=payload/old_row['root']/old_row['path'];good=changed.read_bytes()
    try:
        with changed.open('r+b') as f:f.seek(-1,2);last=f.read(1);f.seek(-1,2);f.write(bytes([last[0]^1]))
        check('modified payload refuses before target writes',run()==25 and snapshot(root)==before)
    finally:changed.write_bytes(good)
    gate.require(gate.sha(changed)==old_row['sha256'],'Cannot restore isolated payload fixture')
    check('actual silent TEST EXE upgrades57 to60',run(nsis=True)==0)
    check('all exact module/support hashes installed',all(target(r).is_file() and gate.sha(target(r))==r['sha256'] for r in rows))
    check('all57 previous module bytes replaced by verified14',all(gate.sha(target(r))==r['sha256'] for r in rows if r['root']=='P' and Path(r['path']).stem in baseline))
    check('new60 ownership receipt created',(root/'Support/Install14/receipt.tsv').is_file())
    check('upgrade preserves unrelated user file',note.read_text()=='keep user file')
    uninstall=root/'Support/Uninstall.exe';runner=OUT/(prefix+'-uninstall.exe');shutil.copy2(uninstall,runner)
    result=subprocess.run([str(runner),'/S','/CASE='+case,'_?='+str(uninstall.parent)],timeout=300,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
    check('actual generated TEST uninstaller succeeds',result.returncode==0)
    check('actual uninstaller removes every shipped file',all(not target(r).exists() for r in rows) and not uninstall.exists() and not (root/'Support/Install14/receipt.tsv').exists())
    check('uninstall preserves unrelated user file',note.read_text()=='keep user file')
    check('live plugin bytes, IDs and timestamps untouched',snapshot(live)==before_live)
    check('production registration untouched',registry()==before_reg)
    check('test and build recipe unchanged during execution',recipe_before==recipe_hashes())
    check('executed helper/setup remain exact',all(gate.sha(build[k])==build[k+'_sha256'] for k in ('helper','installer')))
    gate.verified_manifest()
    report={'schema':1,'release':'14','development_only':False,'passed':all(c['passed'] for c in checks),'checks':checks,
            'installer_sha256':build['installer_sha256'],'helper_sha256':build['helper_sha256'],'payload_sha256':gate.sha(OUT/'payload-build.json'),
            'manifest_sha256':data['manifest_sha256'],'validated_manifest_sha256':data['validated_manifest_sha256'],'recipe_sha256':recipe_before,
            'module_count':60,'updated_count':57,'new_count':3,'live_installation_changed':False,'registry_changed':False,'test_prefix':prefix,
            'transaction_reuse':gate.transaction_reuse()}
    gate.write(OUT/'transaction-tests.json',report);print(json.dumps({'passed':report['passed'],'checks':len(checks)}))

if __name__=='__main__':main()
