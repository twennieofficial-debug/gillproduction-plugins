"""Packaging-only adapter. Never changes the frozen production checkout."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile

FROZEN = '74212023dee2cd5581a9db6fec428677c22f533f'
SOURCE = 'd6f1f13284a9e3c36e3f4fe3a2225578867bc66dd288e75c054a431db0a421d0'
REPOSITORY = 'twennieofficial-debug/gillproduction-plugins'
RUN = 38497542906
INPUTS = {
    'universal': (11872075458, '81b33c2433c4bf0949d49729ee56981b64017c73b197859d15ee9ba741cff429'),
    'validation-arm64': (11874198128, '5724bf0a085f99b118082eacb61fd31c8e9afd6c61514ee259a035bef95d77db'),
    'validation-x86_64': (11873074603, 'aa9d490f9972dbbbfead3d104b257ea04e4d00ca8ffa4ce7e94a73fd131199e1'),
}
INPUT_BYTES = {'universal': 1086070173, 'validation-arm64': 204882, 'validation-x86_64': 204885}

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

def validate_metadata(name, metadata):
    identity, digest = INPUTS[name]
    require(metadata['id'] == identity and metadata['name'] == name, 'Wrong upstream artifact identity')
    require(metadata['digest'] == 'sha256:' + digest and not metadata['expired'], 'Wrong or expired upstream artifact')
    require(metadata['size_in_bytes'] == INPUT_BYTES[name], 'Wrong upstream ZIP length')
    run = metadata['workflow_run']
    require(run['id'] == RUN and run['head_sha'] == FROZEN, 'Wrong upstream run/source commit')

def verify_zip(name, path):
    require(sha(path) == INPUTS[name][1], 'Wrong upstream ZIP bytes: ' + name)
    require(Path(path).stat().st_size == INPUT_BYTES[name], 'Wrong upstream ZIP byte count')

def safe_members(names):
    names = list(names)
    require(len(names) == len(set(names)), 'Duplicate archive member')
    for name in names:
        require(name and not name.startswith('/') and '\\' not in name
                and ':' not in name and '..' not in Path(name).parts, 'Unsafe archive path')

def restore(name, archive, root):
    verify_zip(name, archive)
    destination = root / ('universal' if name == 'universal' else 'validation/' + name.removeprefix('validation-'))
    destination.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as z:
        safe_members(z.namelist())
        require(all((i.external_attr >> 16) & 0o170000 != 0o120000 for i in z.infolist()), 'ZIP symlink forbidden')
        if name == 'universal':
            require(z.namelist() == ['universal.tar.gz'], 'Unexpected Universal ZIP member set')
            z.extract('universal.tar.gz', root)
        else:
            z.extractall(destination)
    if name == 'universal':
        # tar's data filter preserves modes and permits only contained links;
        # the pinned ZIP digest authenticates the entire original tar stream.
        with tarfile.open(root / 'universal.tar.gz') as tar:
            tar.extractall(destination, filter='data')
    return destination

def download_inputs(root):
    records = {}
    for name, (identity, digest) in INPUTS.items():
        endpoint = f'repos/{REPOSITORY}/actions/artifacts/{identity}'
        metadata = json.loads(subprocess.check_output(['gh', 'api', endpoint], text=True))
        validate_metadata(name, metadata)
        archive = root / (name + '.zip')
        with archive.open('xb') as f:
            subprocess.run(['gh', 'api', endpoint + '/zip'], stdout=f, check=True, timeout=900)
        verify_zip(name, archive)
        restore(name, archive, root)
        records[name] = {'artifact_id': identity, 'zip_sha256': digest, 'zip_bytes': archive.stat().st_size,
                         'original_run_id': RUN, 'original_source_commit': FROZEN}
    write(root / 'upstream-inputs.json', records)
    return records

def checked_source(core, source):
    require(git(source.parent, 'rev-parse', 'HEAD') == FROZEN, 'Wrong frozen checkout commit')
    require(not git(source.parent, 'status', '--porcelain'), 'Frozen checkout is dirty')
    report = core.source_check(source)
    require(report['source_sha256'] == SOURCE, 'Wrong frozen source fingerprint')
    return report

class DmgAdapter:
    """Change only create filesystem and retry an actual transient verify error."""
    def __init__(self, original, diagnostics, execute=subprocess.run, sleep=time.sleep):
        self.original, self.diagnostics = original, Path(diagnostics)
        self.execute, self.sleep = execute, sleep
        self.creates, self.verifications = [], []

    def __call__(self, command, *, log=None, timeout=3600):
        command = [str(x) for x in command]
        if command[:2] == ['hdiutil', 'create']:
            require('-fs' not in command and command[-3:-1] == ['-format', 'UDZO'], 'Unexpected original DMG recipe')
            updated = command[:-1] + ['-fs', 'HFS+'] + command[-1:]
            # Do not override an existing output, suppress checks, rename a bad
            # image as good, or retry create against a possibly partial file.
            create_log = self.diagnostics / Path(command[-1]).stem / 'create.txt'
            result = self.original(updated, log=create_log, timeout=timeout)
            self.original(['hdiutil', 'imageinfo', '-plist', command[-1]],
                          log=create_log.with_name('imageinfo.plist'), timeout=timeout)
            self.creates.append({'image': Path(command[-1]).name, 'filesystem': 'HFS+', 'format': 'UDZO'})
            return result
        if command[:2] != ['hdiutil', 'verify']:
            return self.original(command, log=log, timeout=timeout)
        image = Path(command[-1])
        attempts = []
        for attempt in range(1, 4):
            process = self.execute(command, capture_output=True, text=True, timeout=timeout)
            output = process.stdout + process.stderr
            attempt_log = self.diagnostics / image.stem / f'verify-{attempt}.txt'
            attempt_log.parent.mkdir(parents=True, exist_ok=True)
            attempt_log.write_text(output, encoding='utf-8')
            attempts.append({'attempt': attempt, 'exit': process.returncode, 'log': attempt_log.relative_to(self.diagnostics).as_posix(),
                             'log_sha256': sha(attempt_log)})
            if log:
                Path(log).parent.mkdir(parents=True, exist_ok=True)
                Path(log).write_text(output, encoding='utf-8')
            entry = {'image': image.name, 'attempts': attempts, 'passed': process.returncode == 0}
            write(self.diagnostics / image.stem / 'verify-attempts.json', entry)
            if process.returncode == 0:
                self.verifications.append(entry)
                return output
            # hdiutil maps the observed EAGAIN to process exit 1, not errno35.
            # Require its exact terminal diagnostic, not arbitrary text.
            transient = process.returncode == 1 and output.strip().splitlines()[-1:] == [
                'hdiutil: verify failed - Resource temporarily unavailable']
            require(transient and attempt < 3, f'hdiutil verify failed ({process.returncode}): {output}')
            self.sleep(attempt * 2)
        raise AssertionError('unreachable')

def tooling_manifest(tools_root):
    root = tools_root.resolve()
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts and p.suffix in {'.py', '.mjs', '.yml', '.md', '.json'}}

def perform(source, root, workflow):
    sys.path.insert(0, str(source / 'packaging/macos'))
    import pipeline as core
    import individuals
    core.native_arch()
    core.require_disposable_install_runner(root / 'probe.pkg')
    source_before = checked_source(core, source)
    tools_root = Path(__file__).resolve().parent
    tools_repo = Path(git(tools_root, 'rev-parse', '--show-toplevel'))
    require(not git(tools_repo, 'status', '--porcelain'), 'Recovery tooling checkout is dirty')
    tooling_commit = git(tools_repo, 'rev-parse', 'HEAD')
    require(tooling_commit == os.environ.get('GITHUB_SHA'), 'Recovery tooling differs from workflow commit')
    manifest = tooling_manifest(tools_root)
    workflow_sha = sha(workflow)
    upstream = download_inputs(root)
    universal_before = core.tree_manifest(root / 'universal/plugins')
    write(root / 'recovery-evidence/frozen-source.json', source_before)
    write(root / 'recovery-evidence/universal-before.json', universal_before)
    archive = root / 'GILL-QUELLCODE-22.zip'
    core.create_source_archive(argparse.Namespace(source=source, destination=archive))
    delivery = root / 'delivery'
    diagnostics = root / 'recovery-evidence/native-tools'
    diagnostics.mkdir(parents=True)
    for name, command in [('system', ['sw_vers']), ('disks-before', ['diskutil', 'list']), ('images-before', ['hdiutil', 'info'])]:
        core.run(command, log=diagnostics / (name + '.txt'))
    original = core.run
    adapter = DmgAdapter(original, diagnostics)
    core.run = adapter
    args = argparse.Namespace(source=source, universal=root / 'universal', validations=root / 'validation',
        source_archive=archive, staging=root / 'package-staging', destination=delivery,
        test_install=True, notarize=False)
    try:
        result = individuals.package(args)
    finally:
        core.run = original
        write(diagnostics / 'adapter-operations.json', {'created': adapter.creates, 'verified': adapter.verifications})
    source_after = checked_source(core, source)
    require(source_before == source_after, 'Frozen source changed during packaging')
    universal_after = core.tree_manifest(root / 'universal/plugins')
    require(universal_before == universal_after, 'Universal input bundles changed during packaging')
    write(root / 'recovery-evidence/universal-after.json', universal_after)
    require(tooling_manifest(tools_root) == manifest and sha(workflow) == workflow_sha, 'Recovery tools changed during packaging')
    require(len(adapter.creates) == len(adapter.verifications) == len(result['deliveries']) == 75, 'Incomplete recovery delivery')
    require(all(x['installer_execution_passed'] and x['mounted_dmg_passed'] for x in result['deliveries']), 'Incomplete actual tests')
    require({x['image'] for x in adapter.creates} == {x['image'] for x in adapter.verifications}
            == {n for n in result['files'] if n.endswith('.dmg')}, 'Recovery image set mismatch')
    shutil.copy2(archive, delivery / archive.name)
    provenance = {'schema': 1, 'kind': 'mac22-packaging-recovery', 'passed': True, 'packaging_only': True,
        'frozen_source_commit': FROZEN, 'source_sha256': SOURCE, 'upstream_artifacts': upstream,
        'upstream_run_id': RUN, 'original_installer_attempts_passed': False,
        'workflow_tools_commit': tooling_commit, 'recovery_run_id': os.environ['GITHUB_RUN_ID'],
        'recovery_run_attempt': os.environ['GITHUB_RUN_ATTEMPT'], 'tooling_files': manifest,
        'workflow_sha256': workflow_sha, 'filesystem': 'HFS+', 'dmg_format': 'UDZO',
        'actual_installation_count': len(result['deliveries']), 'actual_verified_readonly_mount_count': len(adapter.verifications),
        'release_manifest_sha256': sha(delivery / 'MAC-RELEASE.json'), 'source_archive_sha256': sha(archive),
        'universal_tar_sha256': sha(root / 'universal.tar.gz'),
        'universal_build_sha256': sha(root / 'universal/universal-build.json'),
        'universal_before_sha256': sha(root / 'recovery-evidence/universal-before.json'),
        'universal_after_sha256': sha(root / 'recovery-evidence/universal-after.json'),
        'universal_bundles_unchanged': True, 'frozen_source_proof_sha256': sha(root / 'recovery-evidence/frozen-source.json'),
        'delivered_files': result['files'], 'diagnostic_files': core.tree_manifest(diagnostics)}
    write(root / 'recovery-evidence/recovery-report.json', provenance)
    return provenance

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workflow', type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    perform(args.source.resolve(), root, args.workflow.resolve())

if __name__ == '__main__':
    main()
