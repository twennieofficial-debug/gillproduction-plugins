import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const python = process.env.MAC22_PYTHON || (process.platform === 'win32'
  ? path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe') : 'python3');
const run = spawnSync(python, ['-B', '-m', 'unittest', 'discover', '-s', here, '-p', 'test_*.py', '-v'],
  {encoding:'utf8', windowsHide:true, timeout:120000});
process.stdout.write(run.stdout || '');
process.stderr.write(run.stderr || '');
assert.equal(run.status, 0, 'Recovery rejection controls failed');
const workflow = fs.readFileSync(path.join(here, 'build-macos.yml'), 'utf8');
for (const token of ['workflow_dispatch:', 'runs-on: macos-15', 'ref: 74212023dee2cd5581a9db6fec428677c22f533f',
  'path: frozen', 'path: tools', 'ref: ${{ github.sha }}', 'tools/recovery-tools/recover.py',
  'MAC22-RECOVERY-EVIDENCE', 'GILL-MAC-INSTALLER-UNSIGNIERT-TESTVERSION', 'if: always()', 'contents: read',
  'env GITHUB_SHA=74212023dee2cd5581a9db6fec428677c22f533f', 'frozen/work/packaging/macos/publish22.py']) {
  assert(workflow.includes(token), 'Missing recovery workflow binding: '+token);
}
for (const token of ['continue-on-error', 'git push']) assert(!workflow.includes(token), token);
assert(workflow.indexOf('tools/recovery-tools/recover.py') < workflow.indexOf('frozen/work/packaging/macos/publish22.py'));
assert(fs.readFileSync(path.join(here, 'REVIEW.md'), 'utf8').includes('Pass 4'));
console.log('MAC22_PACKAGING_RECOVERY_RECIPE_VERIFIED');
