from pathlib import Path
import subprocess,json,hashlib
from datetime import datetime,timezone
base=Path('E:/GILLPRODUCTION/Development14');root=Path(__file__).resolve().parents[1];folder=base/'MixRealHost';exe=folder/'GillMixRealHost.exe'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
products=[]
for name in ('GILLMIX','GILLLINK'):
 bundle=base/'MIX'/(name+'_artefacts/Release/VST3')/(name+'.vst3');module=bundle/'Contents/x86_64-win'/(name+'.vst3');products.append(dict(name=name,version='0.14.0',bundle=str(bundle),module=str(module),sha256=sha(module)))
proof=dict(schema=1,release='14',origin='updated14',passed=False,products_unchanged=False,host_executable=str(exe),host_executable_sha256=sha(exe),host_source_sha256=sha(root/'GILLMIX/Tests/RealHost.cpp'),products=products,runs=[])
for rate in (44100,48000,96000,192000):
 path=folder/f'real-host-{rate}.json';assert not path.exists()
 print('REAL MIX HOST '+str(rate),flush=True)
 with (folder/f'real-host-{rate}.log').open('wb') as out:result=subprocess.run([str(exe),products[0]['bundle'],products[1]['bundle'],str(rate),str(path)],stdout=out,stderr=subprocess.STDOUT,timeout=180)
 actual=json.loads(path.read_text()) if path.exists() else {};unchanged=all(sha(r['module'])==r['sha256'] for r in products)
 assert result.returncode==0 and actual.get('passed') is True and unchanged,(rate,result.returncode,actual)
 proof['runs'].append(dict(sample_rate=rate,exit_code=result.returncode,report=str(path),report_sha256=sha(path),result=actual))
proof.update(passed=True,products_unchanged=True,completed_at=datetime.now(timezone.utc).isoformat());(folder/'real-host-matrix.json').write_text(json.dumps(proof,indent=2));print('MIX host passed all four rates',flush=True)
