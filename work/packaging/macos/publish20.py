#!/usr/bin/env python3
"""Publish exact native-tested Mac20 bytes. No overwrite, deletion or credentials in logs."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import individuals
import pipeline as core

REPO = 'twennieofficial-debug/gillproduction-plugins'
TAG = 'mac-bundle-20'
URL = f'https://github.com/{REPO}/releases/tag/{TAG}'
ARCHIVE = 'GILL-QUELLCODE-20.zip'
require = core.require


def checked_file(path):
    require(path.is_file() and not path.is_symlink(), f'Missing or linked file: {path.name}')
    return path


def verify_local(args):
    """Recheck native evidence; synthetic unit fixtures never exercise this gate."""
    core.native_arch()
    source, delivery, universal = (Path(p).resolve() for p in (args.source, args.delivery, args.universal))
    sha = os.environ.get('GITHUB_SHA', '')
    require(os.environ.get('GITHUB_REPOSITORY') == REPO and re.fullmatch('[0-9a-f]{40}', sha), 'Wrong repository or commit')
    require(core.run(['git', '-C', source.parent, 'rev-parse', 'HEAD']).strip() == sha, 'Checkout differs from GitHub commit')
    core.run(['git', '-C', source.parent, 'diff', '--exit-code', 'HEAD', '--'])
    report = individuals.verify_inputs(universal, args.validations, core.PRODUCTS)
    source_report = core.source_check(source)
    require(report['source_sha256'] == source_report['source_sha256'], 'Native source differs from checkout')
    archive = checked_file(Path(args.source_archive).resolve())
    require(archive.name == ARCHIVE, 'Frozen source ZIP name differs')
    core.verify_source_archive(archive, source_report)
    manifest = core.read(checked_file(delivery / 'MAC-RELEASE.json'))
    assets = validate_delivery(delivery, manifest, report, archive)
    assets += [delivery / 'MAC-RELEASE.json', checked_file(universal / 'universal-build.json'), archive]
    return assets, sha, manifest['source_sha256']


def validate_delivery(delivery, manifest, universal, archive):
    """Structural/byte checks complement, never replace, the native release gate."""
    names = {p['name'] for p in core.PRODUCTS}
    individuals.validate_catalog(core.PRODUCTS)
    expected_flags = {'release': '20', 'product_count': 74, 'gill_bundle_count': 73,
        'individual_dmg_count': 74, 'dmg_count': 75, 'architectures': ['arm64', 'x86_64'],
        'notarized': False, 'end_user_release_ready': False, 'fl_studio_tested': False,
        'signing': 'ad-hoc-local-test-only', 'installer_and_dmg_signing': 'unsigned',
        'native_validation_passed': True, 'installer_execution_passed': True}
    for key, value in expected_flags.items():
        require(type(manifest.get(key)) is type(value) and manifest[key] == value, f'Release field mismatch: {key}')
    require(manifest['source_sha256'] == universal['source_sha256'] and universal['signing'] == 'ad-hoc-local-test-only', 'Wrong native source/signing')
    require(manifest['source_archive_sha256'] == core.sha(archive), 'Source ZIP changed')
    records = universal['plugins']
    require(len(records) == 74 and {p['name'] for p in records} == names, 'Universal products differ')
    by_name = {p['name']: p for p in records}
    deliveries = manifest['deliveries']
    require(len(deliveries) == 75 and {d['label'] for d in deliveries} == names | {'GILL-73-BUNDLE'}, 'Missing/duplicate delivery')
    files, assets = {}, []
    for item in deliveries:
        label = item['label']
        selected = names - {'MOBINA'} if label == 'GILL-73-BUNDLE' else {label}
        require(len(item['products']) == len(selected) and set(item['products']) == selected, 'Wrong bundle membership')
        require(item['installer_execution_passed'] is True and item['mounted_dmg_passed'] is True, 'Untested delivery')
        base = f'{label}-20-MAC-UNSIGNIERT-TESTVERSION'
        pkg, dmg = (delivery / (base + ext) for ext in ('.pkg', '.dmg'))
        require(set(item['files']) == {pkg.name, dmg.name}, 'Unexpected delivery filename')
        for path in (pkg, dmg):
            checked_file(path)
            digest = core.sha(path)
            require(item['files'][path.name] == digest, f'Changed delivery: {path.name}')
            files[path.name] = digest
        with dmg.open('rb') as stream:
            require(dmg.stat().st_size >= 512, 'Truncated DMG')
            stream.seek(-512, 2)
            require(stream.read(4) == b'koly', 'Missing Apple UDIF trailer')
        with pkg.open('rb') as stream:
            require(stream.read(4) == b'xar!', 'Missing Apple package archive header')
        evidence = delivery / 'verification' / label
        installed = core.read(checked_file(evidence / 'installer-test.json'))
        require(installed['passed'] is True and installed['unrelated_plugins_unchanged'] is True
                and installed['installer_sha256'] == files[pkg.name], 'Installer evidence differs')
        require(len(installed['plugins']) == len(selected)
                and {p['name'] for p in installed['plugins']} == selected
                and all(p == by_name[p['name']] for p in installed['plugins']), 'Installed binary evidence differs')
        mounted = core.read(checked_file(evidence / 'mounted-dmg-test.json'))
        require(mounted['passed'] is True and mounted['readonly_mount'] is True
                and mounted['verified_packaged_pkg_bytes'] is True and mounted['dmg_sha256'] == files[dmg.name], 'Mounted DMG evidence differs')
        require(mounted['files'][pkg.name]['sha256'] == files[pkg.name], 'Mounted PKG differs')
        if label == 'GILL-73-BUNDLE':
            require(mounted['files'][ARCHIVE]['sha256'] == manifest['source_archive_sha256'], 'Bundle source ZIP differs')
        assets.append(dmg)
        if label in ('GILL-73-BUNDLE', 'MOBINA'):
            assets.append(pkg)
    require(manifest['files'] == files and len(files) == 150, 'Release file set differs')
    require({p.name for p in delivery.glob('*.dmg')} == {n for n in files if n.endswith('.dmg')}, 'Extra DMG')
    return assets


class GitHub:
    """gh receives auth solely through its inherited environment. Errors omit stderr."""
    def api(self, path, method='GET', payload=None, missing=False):
        cmd = ['gh', 'api', path, '--hostname', 'github.com', '--method', method, '-H', 'Accept: application/vnd.github+json', '-H', 'X-GitHub-Api-Version: 2022-11-28']
        if payload is not None:
            cmd += ['--input', '-']
        result = subprocess.run(cmd, input=json.dumps(payload) if payload is not None else None,
                                capture_output=True, text=True, timeout=300, check=False)
        if result.returncode:
            if missing and re.search(r'\(HTTP 404\)', result.stderr):
                return None
            raise RuntimeError(f'GitHub API {method} failed; release preserved for inspection/retry')
        return json.loads(result.stdout) if result.stdout.strip() else None

    def upload(self, release_id, path):
        from urllib.parse import quote
        endpoint = f'https://uploads.github.com/repos/{REPO}/releases/{release_id}/assets?name={quote(path.name, safe="")}'
        result = subprocess.run(['gh', 'api', endpoint, '--method', 'POST', '-H', 'Content-Type: application/octet-stream',
                                 '--input', str(path)], capture_output=True, timeout=3600, check=False)
        require(result.returncode == 0, 'Asset upload failed; retry safely resumes the draft')


def tag_sha(api):
    ref = api.api(f'repos/{REPO}/git/ref/tags/{TAG}', missing=True)
    if ref is None:
        return None
    obj = ref['object']
    for _ in range(8):
        if obj['type'] == 'commit':
            return obj['sha']
        require(obj['type'] == 'tag', 'Unexpected release tag object')
        obj = api.api(f'repos/{REPO}/git/tags/{obj["sha"]}')['object']
    raise RuntimeError('Release tag nesting exceeds limit')


def remote_assets(api, release_id):
    assets = []
    for page in range(1, 100):
        batch = api.api(f'repos/{REPO}/releases/{release_id}/assets?per_page=100&page={page}')
        assets += batch
        if len(batch) < 100:
            return assets
    raise RuntimeError('Unexpected remote asset count')


def verify_remote(assets, expected, complete):
    seen = set()
    for item in assets:
        name = item['name']
        require(name in expected and name not in seen, 'Extra/duplicate remote release asset')
        digest, size = expected[name]
        require(item.get('state') == 'uploaded' and item.get('digest') == 'sha256:' + digest
                and item.get('size') == size, f'Remote asset differs or digest unavailable: {name}')
        seen.add(name)
    require(not complete or seen == set(expected), 'Remote release asset set incomplete')
    return seen


def publish(api, assets, commit, source_digest):
    require(re.fullmatch('[0-9a-f]{40}', commit) and re.fullmatch('[0-9a-f]{64}', source_digest), 'Invalid provenance')
    require(len({p.name for p in assets}) == len(assets), 'Duplicate local asset name')
    expected = {p.name: (core.sha(checked_file(p)), p.stat().st_size) for p in assets}
    marker = f'Mac20 source commit: {commit}\nSource SHA256: {source_digest}'
    target = tag_sha(api)
    require(target in (None, commit), 'Existing tag points to another commit')
    endpoint = f'repos/{REPO}/releases'
    release = api.api(f'{endpoint}/tags/{TAG}', missing=True)
    if release is None:
        release = api.api(endpoint, 'POST', {'tag_name': TAG, 'target_commitish': commit, 'name': 'Mac20 – 74 Universal VST3 Plugins (unsignierte Testversion)',
            'draft': True, 'prerelease': True, 'body': marker + '\n\n75 DMGs: 74 einzelne Plugins und ein GILL-Bundle mit 73 Plugins. MOBINA separat.\nApple Silicon + Intel, macOS 11+. VST3 nur ad-hoc signiert; PKG/DMG ohne Developer ID und ohne Notarisierung. Native automatisierte Tests bestanden; keine FL-Studio-Hörprüfung und keine Endkundenfreigabe. Quellcode und SHA256SUMS.txt beigefügt.'})
    require(release['tag_name'] == TAG and marker in (release.get('body') or '') and release.get('target_commitish') == commit, 'Existing release provenance differs')
    # GitHub may defer creating a draft's tag until publication. A present tag must always match.
    require(tag_sha(api) in (None, commit), 'Tag changed during release preparation')
    current = verify_remote(remote_assets(api, release['id']), expected, complete=not release['draft'])
    if not release['draft']:
        require(tag_sha(api) == commit, 'Published tag differs')
        return URL
    for path in sorted(assets, key=lambda p: p.name):
        require((core.sha(checked_file(path)), path.stat().st_size) == expected[path.name], 'Local asset changed during publication')
        if path.name not in current:
            api.upload(release['id'], path)
    verify_remote(remote_assets(api, release['id']), expected, complete=True)
    require(tag_sha(api) in (None, commit), 'Tag changed before publication')
    for path in assets:
        require((core.sha(checked_file(path)), path.stat().st_size) == expected[path.name], 'Local asset changed before publication')
    updated = api.api(f'{endpoint}/{release["id"]}', 'PATCH', {'draft': False, 'prerelease': True, 'make_latest': 'false', 'target_commitish': commit})
    require(updated['draft'] is False and tag_sha(api) == commit, 'Publication could not be confirmed')
    verify_remote(remote_assets(api, release['id']), expected, complete=True)
    return URL


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('delivery', 'source', 'source-archive', 'universal', 'validations'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    require(bool(os.environ.get('GH_TOKEN')), 'GH_TOKEN required')
    assets, commit, source_digest = verify_local(args)
    sums = Path(args.delivery).resolve() / 'SHA256SUMS.txt'
    text = ''.join(f'{core.sha(p)}  {p.name}\n' for p in sorted(assets, key=lambda p: p.name))
    if sums.exists():
        require(checked_file(sums).read_text(encoding='utf-8') == text, 'Existing checksums differ')
    else:
        sums.write_text(text, encoding='utf-8', newline='\n')
    print(publish(GitHub(), assets + [sums], commit, source_digest))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'STOP: {error}', file=sys.stderr)
        sys.exit(1)
