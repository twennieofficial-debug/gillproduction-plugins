import {existsSync,mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname,resolve,join,delimiter} from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const here=dirname(fileURLToPath(import.meta.url)), root=resolve(here,'../../../..');
const bin=join(root,'toolchain/llvm-mingw-20260922-ucrt-x86_64/bin');
const compiler=process.env.CXX || (process.platform==='win32'?join(bin,'x86_64-w64-mingw32-clang++.exe'):'c++');
const output=join(here,'build');mkdirSync(output,{recursive:true});
const exe=join(output,process.platform==='win32'?'spectral-tests.exe':'spectral-tests');
const env={...process.env,PATH:bin+delimiter+process.env.PATH};
const compile=spawnSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',join(here,'spectral_tests.cpp'),'-o',exe],{env,encoding:'utf8',timeout:120000});
if(compile.status!==0){process.stderr.write(compile.stdout??'');process.stderr.write(compile.stderr??'');throw Error('Spectral test compilation failed: '+(compile.error?.message??compile.status));}
const run=spawnSync(exe,[],{env,encoding:'utf8',timeout:120000});
process.stdout.write(run.stdout??'');process.stderr.write(run.stderr??'');
const success=run.status===0&&/^SPECTRAL_AUDIO_TESTS_OK$/m.test(run.stdout??'');
const header=resolve(here,'../../Source/Spectral/Spectral.h');
writeFileSync(join(output,'result.json'),JSON.stringify({success,exitCode:run.status,
 sourceSHA256:createHash('sha256').update(readFileSync(header)).digest('hex'),
 testSHA256:createHash('sha256').update(readFileSync(join(here,'spectral_tests.cpp'))).digest('hex'),
 command:[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static'],stdout:run.stdout,stderr:run.stderr},null,2));
if(!success)throw Error('Spectral audio fixtures failed');
console.log('SPECTRAL_CORE_PASS');
