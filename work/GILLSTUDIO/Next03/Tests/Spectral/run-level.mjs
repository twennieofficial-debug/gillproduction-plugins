import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname,resolve,join,delimiter} from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const here=dirname(fileURLToPath(import.meta.url)),work=resolve(here,'../../../..'),output=join(here,'build');mkdirSync(output,{recursive:true});
const bin=join(work,'toolchain/llvm-mingw-20260922-ucrt-x86_64/bin'),compiler=join(bin,'x86_64-w64-mingw32-clang++.exe'),env={...process.env,PATH:bin+delimiter+process.env.PATH};
const exe=join(output,'spectra-level.exe'),cpp=join(here,'level_tests.cpp'),header=resolve(here,'../../Source/Spectral/Spectral.h'),legacy=resolve(here,'../../../Source/Spectral/Spectral.h');
const sha=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
if(sha(legacy)!=='cb8a5b610879d0062cc17a5fbb199e37dac6dd8107b9676096dd0a2bde03e863')throw Error('Frozen legacy core changed');
const build=spawnSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',cpp,'-o',exe],{env,encoding:'utf8',timeout:120000});
if(build.status!==0){process.stdout.write(build.stdout??'');process.stderr.write(build.stderr??'');throw Error('Level test build failed '+(build.error??build.status));}
const run=spawnSync(exe,[],{env,encoding:'utf8',timeout:180000});process.stdout.write(run.stdout??'');process.stderr.write(run.stderr??'');
const success=run.status===0&&/^SPECTRA_LEVEL_AUDIO_PASS$/m.test(run.stdout??'');
writeFileSync(join(output,'level-result.json'),JSON.stringify({success,exit:run.status,error:run.error?.message,sourceSHA256:sha(header),testSHA256:sha(cpp),runnerSHA256:sha(fileURLToPath(import.meta.url)),legacySHA256:sha(legacy),binarySHA256:sha(exe),stdout:run.stdout,stderr:run.stderr},null,2));
if(!success)throw Error('Level audio fixtures failed');
