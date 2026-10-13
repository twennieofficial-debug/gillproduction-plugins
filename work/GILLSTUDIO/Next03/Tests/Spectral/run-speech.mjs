import {spawnSync} from 'node:child_process';
import {dirname,join,resolve,delimiter} from 'node:path';
import {fileURLToPath} from 'node:url';
import {mkdirSync} from 'node:fs';
const here=dirname(fileURLToPath(import.meta.url)),work=resolve(here,'../../../..');
const bin=join(work,'toolchain/llvm-mingw-20260922-ucrt-x86_64/bin');
const env={...process.env,PATH:bin+delimiter+process.env.PATH};
mkdirSync(join(here,'build'),{recursive:true});
const commands=[
 [join(bin,'x86_64-w64-mingw32-clang++.exe'),['-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',join(here,'render_fixture.cpp'),'-o',join(here,'build/render-fixture.exe')]],
 ['C:/Users/iangi/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',[join(here,'speech_quality.py')]]
];
for(const[cmd,args]of commands){const r=spawnSync(cmd,args,{env,encoding:'utf8',timeout:120000});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error||r.status!==0)throw Error(r.error?.message??`Speech quality step failed ${r.status}`);}
