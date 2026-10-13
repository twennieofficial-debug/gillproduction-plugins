import {spawnSync} from 'node:child_process';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';
const here=dirname(fileURLToPath(import.meta.url));
for(const [script,token] of [['run.mjs','SPECTRAL_CORE_PASS'],['run-advanced.mjs','SPECTRA_ADVANCED_PASS'],['run-speech.mjs','SPEECH_QUALITY_PASS']]){
 const r=spawnSync(process.execPath,[join(here,script)],{encoding:'utf8',timeout:240000});
 process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');
 if(r.status!==0||!(r.stdout??'').split(/\r?\n/).includes(token))throw Error('Regression failed: '+script+' '+r.status);
}
console.log('SPECTRA03_CORE_REGRESSIONS_PASS');
