"""Fetch the exact reviewed, failed MIX-host donor without changing its evidence."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys

REPOSITORY = 'twennieofficial-debug/gillproduction-plugins'
RUN_ID = 38013907475
COMMIT = '61d37b2e7fcbc3cad371a4b1094b2355f3238e7b'
SOURCE_SHA = 'a566e14257cdb00bfaac0521a45746e129cf8baeec284327a61ce0e816ebd885'
HOST_PATH = 'work/GILLMIX/Tests/RealHost.cpp'
HOST_SHA = 'f2b7099291ad3260ef3ba77e7d9d1fe7780d64d52212530f6dcb48a385b90635'
JOBS = {'arm64':'native-build (macos-15, arm64)', 'x86_64':'native-build (macos-15-intel, x86_64)'}

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def validate_inputs(repository, run_id, commit, source_sha, architecture):
    require(repository == REPOSITORY and run_id == RUN_ID and commit == COMMIT and source_sha == SOURCE_SHA,
            'Only the explicitly reviewed MIX-host donor is allowed.')
    require(architecture in JOBS, 'Unsupported native architecture.')

def validate_metadata(run, jobs, artifacts, architecture):
    require(run.get('id') == RUN_ID and run.get('head_sha') == COMMIT, 'Donor run/commit differs.')
    for field in ('repository', 'head_repository'):
        require(run.get(field, {}).get('full_name') == REPOSITORY, 'Donor repository differs.')
    require(run.get('event') == 'workflow_dispatch' and
            run.get('path', '').split('@', 1)[0] == '.github/workflows/build-macos.yml', 'Donor workflow differs.')
    require(jobs.get('total_count') == len(jobs.get('jobs', [])), 'Incomplete job listing.')
    require(artifacts.get('total_count') == len(artifacts.get('artifacts', [])), 'Incomplete artifact listing.')
    matches = [j for j in jobs['jobs'] if j.get('name') == JOBS[architecture]]
    require(len(matches) == 1 and matches[0].get('status') == 'completed' and matches[0].get('conclusion') == 'failure',
            'Selected native donor job must have completed with retained diagnostic evidence.')
    selected = {}
    for name in ('failed-native-' + architecture, 'failed-native-binaries-' + architecture):
        matching = [a for a in artifacts['artifacts'] if a.get('name') == name]
        require(len(matching) == 1, 'Missing or duplicate diagnostic artifact: ' + name)
        artifact = matching[0]
        require(artifact.get('expired') is False and type(artifact.get('id')) is int and artifact['id'] > 0,
                'Invalid or expired donor artifact.')
        identity = artifact.get('workflow_run', {})
        require(identity.get('id') == RUN_ID and identity.get('head_sha') == COMMIT, 'Donor artifact origin differs.')
        require(type(artifact.get('size_in_bytes')) is int and 0 < artifact['size_in_bytes'] < 512 * 1024**2,
                'Invalid or excessive diagnostic transport size.')
        require(re.fullmatch(r'sha256:[0-9a-f]{64}', artifact.get('digest', '')) is not None,
                'GitHub diagnostic transport digest is missing.')
        selected[name] = artifact
    return {'run':run, 'job':matches[0], 'artifacts':selected}

def api(endpoint):
    result = subprocess.run(['gh', 'api', endpoint], capture_output=True, timeout=120)
    require(result.returncode == 0, 'GitHub metadata request failed; authentication output withheld.')
    return json.loads(result.stdout)

def download(artifact, destination):
    with destination.open('xb') as stream:
        result = subprocess.run(['gh', 'api', f'repos/{REPOSITORY}/actions/artifacts/{artifact["id"]}/zip'],
                                stdout=stream, stderr=subprocess.PIPE, timeout=600)
    require(result.returncode == 0, 'GitHub artifact download failed; authentication output withheld.')
    require(destination.stat().st_size == artifact['size_in_bytes'], 'Diagnostic transport byte size differs.')
    digest = hashlib.sha256()
    with destination.open('rb') as stream:
        for block in iter(lambda: stream.read(1024**2), b''):
            digest.update(block)
    require('sha256:' + digest.hexdigest() == artifact['digest'], 'Diagnostic transport SHA256 differs.')

def original_host(record):
    require(record.get('type') == 'file' and record.get('path') == HOST_PATH and record.get('encoding') == 'base64',
            'Original host API record differs.')
    compact = ''.join(record.get('content', '').split())
    raw = base64.b64decode(compact, validate=True)
    require(hashlib.sha256(raw).hexdigest() == HOST_SHA, 'Original host bytes differ from reviewed source.')
    return raw

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--base-run', type=int, required=True)
    parser.add_argument('--base-commit', required=True)
    parser.add_argument('--base-source-sha', required=True)
    parser.add_argument('--architecture', choices=tuple(JOBS), required=True)
    parser.add_argument('--destination', required=True)
    args = parser.parse_args()
    validate_inputs(args.repository, args.base_run, args.base_commit, args.base_source_sha, args.architecture)
    prefix = f'repos/{REPOSITORY}/actions/runs/{RUN_ID}'
    provenance = validate_metadata(api(prefix), api(prefix + '/jobs?per_page=100'),
                                   api(prefix + '/artifacts?per_page=100'), args.architecture)
    raw_host = original_host(api(f'repos/{REPOSITORY}/contents/{HOST_PATH}?ref={COMMIT}'))
    folder = Path(args.destination).resolve()
    require(not folder.exists(), 'Preserve existing donor quarantine; choose a fresh destination.')
    folder.mkdir(parents=True)
    for name, artifact in provenance['artifacts'].items():
        download(artifact, folder / (name + '.zip'))
    (folder / 'original-RealHost.cpp').write_bytes(raw_host)
    (folder / 'provenance.json').write_text(json.dumps(provenance, indent=2), encoding='utf-8')
    print('MIX_HOST_DONOR_TRANSPORT_VERIFIED ' + args.architecture)

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print('STOP: ' + str(error), file=sys.stderr)
        sys.exit(1)
