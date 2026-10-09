"""Mac20 delivery: 74 single-product DMGs and a separate 73-product GILL DMG.

Call package(args) with the pipeline package namespace. Native Apple tools and
disposable GitHub-hosted macOS are mandatory; local unit tests are recipe tests.
"""
from __future__ import annotations

import os
from pathlib import Path
import plistlib
import re
import shutil
import xml.etree.ElementTree as ET

import pipeline as core

VST3 = Path('/Library/Audio/Plug-Ins/VST3')
RELEASE_URL = 'https://github.com/twennieofficial-debug/gillproduction-plugins/releases/tag/mac-bundle-20.1'
STATUS = 'UNSIGNIERT-TESTVERSION'


def safe_name(value):
    core.require(isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 _.-]*', value)
                 and value not in {'.', '..'} and value == value.strip()
                 and not value.endswith('.'), f'Unsafe product/path name: {value!r}')
    return value


def install_relative(product):
    vendor = 'MOBINA' if product['name'] == 'MOBINA' else 'GILLPRODUCTION'
    return Path('Library/Audio/Plug-Ins/VST3') / vendor / (safe_name(product['name']) + '.vst3')


def validate_catalog(products):
    core.require(len(products) == 74, 'Mac20 requires exactly 74 products')
    for key in ('name', 'target', 'bundle_id'):
        core.require(len({p[key].casefold() for p in products}) == len(products), f'Duplicate {key}')
    core.require(sum(p['name'] == 'MOBINA' for p in products) == 1, 'Exactly one MOBINA required')
    for p in products:
        safe_name(p['name'])
        safe_name(p['target'])
        core.require(re.fullmatch(r'[A-Za-z0-9.-]+', p['bundle_id']) is not None, 'Unsafe bundle identifier')
        core.require(p['version'] == '0.20.0', 'Mac20 catalog version mismatch')
        core.require(not Path(p['group']).is_absolute() and not any(x in ('..', '.', '') for x in p['group'].replace('\\', '/').split('/')), 'Unsafe source group')
    return [p for p in products if p['name'] != 'MOBINA']


def verify_inputs(universal, validations, products):
    validate_catalog(products)
    root = Path(universal)
    expected = {p['name'] for p in products}
    core.require({p.name for p in (root / 'plugins').iterdir()} == {n + '.vst3' for n in expected}, 'Missing or extra universal product')
    report = core.verify_release_gate(root, validations)
    records = report.get('plugins', [])
    core.require(len(records) == len(expected) and {p['name'] for p in records} == expected, 'Universal report product mismatch')
    for arch in ('arm64', 'x86_64'):
        validation = core.read(Path(validations) / arch / f'validation-{arch}.json')
        core.require(len(validation['plugins']) == len(expected) and {p['name'] for p in validation['plugins']} == expected, 'Validation product mismatch')
    by_name = {r['name']: r for r in records}
    for product in products:
        bundle = root / 'plugins' / (product['name'] + '.vst3')
        core.require(not bundle.is_symlink(), 'Top-level plugin symlink forbidden')
        actual = core.inspect_bundle(bundle, product, ['arm64', 'x86_64'])
        core.require(actual == by_name[product['name']], 'Universal manifest/hash/architecture mismatch')
        core.run(['codesign', '--verify', '--strict', '--deep', bundle])
    return report


def copy_licenses(source, products, target):
    source, target = Path(source).resolve(), Path(target)
    for group in sorted({p['group'] for p in products}):
        project = source / group
        core.require(project.resolve().is_relative_to(source), 'License source escapes workspace')
        for path in project.rglob('*'):
            relative = path.relative_to(project)
            if any(part.startswith('build') or part == '.git' for part in relative.parts):
                continue
            if path.is_file() and path.name.upper().startswith(('LICENSE', 'NOTICE', 'COPYING', 'THIRD-PARTY')):
                core.require(path.resolve().is_relative_to(source), 'License symlink escapes workspace')
                destination = target / group / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)
    juce = target / 'JUCE/LICENSE.md'
    juce.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'dependencies/JUCE/LICENSE.md', juce)
    core.require(any(target.rglob('*')), 'License notices missing')


def write_documents(folder, selected, archive, *, bundle, archive_sha=None):
    folder.mkdir(parents=True, exist_ok=True)
    locations = '\n'.join('/' + install_relative(p).as_posix() for p in selected)
    (folder / 'ZUERST-LESEN.txt').write_text(
        'GILLPRODUCTION / MOBINA – Mac20, Universal VST3 (Apple Silicon und Intel)\n'
        'macOS 11 oder neuer. DAW vor der Installation beenden.\n'
        'DMG öffnen und die enthaltene PKG mit dem macOS Installer installieren.\n'
        'Danach in FL Studio den Plugin Manager öffnen und Plugins neu suchen.\n'
        'Installationsorte:\n' + locations + '\n\n'
        'UNSIGNIERT-TESTVERSION: Die VST3-Dateien sind nur ad-hoc signiert.\n'
        'PKG und DMG sind nicht mit Apple Developer ID signiert oder notarisiert.\n'
        'Gatekeeper kann die Installation blockieren. Keine Sicherheitsprüfungen deaktivieren.\n'
        'Eine eigene FL-Studio-Hörprüfung auf einem echten Mac ist noch erforderlich.\n'
        'Zum Entfernen nur die oben genannten VST3-Bundles löschen; keine Projekte löschen.\n', encoding='utf-8')
    (folder / 'QUELLCODE.txt').write_text(
        'Vollständiger zugehöriger Quellcode einschließlich JUCE:\n'
        f'Datei: {archive.name}\nSHA256: {archive_sha or core.sha(archive)}\n'
        f'Download: {RELEASE_URL}\n'
        + ('Die ZIP-Datei liegt auch in dieser DMG.\n' if bundle else 'Die identische ZIP wird separat zusammen mit dieser Version veröffentlicht und liegt in der GILL-Bundle-DMG.\n'), encoding='utf-8')
    core.write(folder / 'products.json', selected)


def make_pkg(payload, selected, work, destination, identifier):
    components = work / 'components.plist'
    core.run(['pkgbuild', '--analyze', '--root', payload, components])
    definitions = plistlib.loads(components.read_bytes())
    expected = {install_relative(p).as_posix() for p in selected}
    core.require(len(definitions) == len(expected) and {d['RootRelativeBundlePath'] for d in definitions} == expected, 'pkgbuild discovered wrong bundles')
    for d in definitions:
        d.update(BundleIsRelocatable=False, BundleIsVersionChecked=False, BundleOverwriteAction='upgrade')
    components.write_bytes(plistlib.dumps(definitions))
    component = work / 'payload.pkg'
    core.run(['pkgbuild', '--root', payload, '--component-plist', components, '--identifier', identifier,
              '--version', '0.20.0', '--install-location', '/', '--ownership', 'recommended', component])
    distribution = ET.Element('installer-gui-script', {'minSpecVersion': '2'})
    ET.SubElement(distribution, 'title').text = 'GILL Mac20 – ' + (selected[0]['name'] if len(selected) == 1 else '73 GILL Plugins')
    ET.SubElement(distribution, 'options', {'customize': 'never', 'require-scripts': 'false', 'hostArchitectures': 'arm64,x86_64'})
    ET.SubElement(distribution, 'domains', {'enable_anywhere': 'false', 'enable_currentUserHome': 'false', 'enable_localSystem': 'true'})
    ET.SubElement(ET.SubElement(distribution, 'allowed-os-versions'), 'os-version', {'min': '11.0'})
    ET.SubElement(ET.SubElement(distribution, 'choices-outline'), 'line', {'choice': 'plugins'})
    choice = ET.SubElement(distribution, 'choice', {'id': 'plugins', 'visible': 'false'})
    ET.SubElement(choice, 'pkg-ref', {'id': identifier})
    ET.SubElement(distribution, 'pkg-ref', {'id': identifier, 'version': '0.20.0'}).text = component.name
    xml = work / 'Distribution.xml'
    ET.ElementTree(distribution).write(xml, encoding='utf-8', xml_declaration=True)
    core.run(['productbuild', '--distribution', xml, '--package-path', work, destination])
    # Product/component identifiers and fixed installation root are checked from
    # the final product archive, without expanding large audio payloads.
    expanded = work / 'expanded'
    core.run(['pkgutil', '--expand', destination, expanded])
    infos = list(expanded.rglob('PackageInfo'))
    core.require(len(infos) == 1, 'Final PKG must contain exactly one component')
    info = ET.parse(infos[0]).getroot()
    core.require(info.get('identifier') == identifier and info.get('version') == '0.20.0'
                 and info.get('install-location') == '/', 'Final PKG metadata mismatch')
    core.require(not list(expanded.rglob('Scripts')), 'Installer scripts are forbidden')


def test_install(pkg, universal, selected, payload, evidence, *, cleanup):
    core.require_disposable_install_runner(pkg)
    selected_paths = [Path('/') / install_relative(p) for p in selected]
    core.require(all(not p.exists() and not p.is_symlink() for p in selected_paths), 'Selected plugin must be absent before install test')
    before = core.tree_manifest(VST3) if VST3.exists() else {}
    expected = core.tree_manifest(payload)
    report = {'passed': False, 'installer_sha256': core.sha(pkg), 'plugins': [], 'unrelated_plugins_unchanged': False}
    try:
        core.run(['sudo', '-n', '/usr/sbin/installer', '-pkg', pkg, '-target', '/'], log=evidence / 'installer-console.txt', timeout=900)
        for relative, record in expected.items():
            installed = Path('/') / relative
            core.require(installed.exists() or installed.is_symlink(), f'Missing installed file: {relative}')
            actual = ({'symlink': os.readlink(installed)} if installed.is_symlink() else
                      {'sha256': core.sha(installed), 'executable': bool(installed.stat().st_mode & 0o111)})
            core.require(actual == record, f'Installed file differs: {relative}')
        for product, path in zip(selected, selected_paths):
            core.require(core.tree_manifest(path) == core.tree_manifest(Path(universal) / 'plugins' / path.name), 'Installed bundle file set differs')
            record = core.inspect_bundle(path, product, ['arm64', 'x86_64'])
            core.run(['codesign', '--verify', '--strict', '--deep', path])
            report['plugins'].append(record)
        after = core.tree_manifest(VST3)
        prefixes = [p.relative_to(VST3).as_posix() + '/' for p in selected_paths]
        unrelated = {k: v for k, v in after.items() if not any(k.startswith(prefix) for prefix in prefixes)}
        core.require(unrelated == before, 'Installer modified unrelated VST3 files')
        report.update(passed=True, unrelated_plugins_unchanged=True, payload_file_count=len(expected))
    finally:
        core.write(evidence / 'installer-test.json', report)
    if cleanup:
        # Only fresh, verified test installations on the disposable VM; no
        # recursive deletion of a computed parent or pre-existing plugin.
        for path in selected_paths:
            core.require(path.parent in (VST3 / 'GILLPRODUCTION', VST3 / 'MOBINA') and path.suffix == '.vst3'
                         and not path.is_symlink() and path.resolve() == path, 'Unsafe cleanup path')
            core.run(['sudo', '-n', 'rm', '-rf', '--', path])
        core.require((core.tree_manifest(VST3) if VST3.exists() else {}) == before, 'Plugin cleanup changed unrelated files')
    return report


def test_dmg(dmg, disk, mount, evidence):
    core.run(['hdiutil', 'verify', dmg], log=evidence / 'dmg-verify.txt')
    mount.mkdir(parents=True, exist_ok=True)
    attached = False
    try:
        core.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mount, dmg])
        attached = True
        expected = core.tree_manifest(disk)
        actual = core.tree_manifest(mount)
        # HFS+ can add hidden filesystem metadata, but every shipped file must
        # match and no additional visible deliverable may appear.
        visible = {k: v for k, v in actual.items() if not k.split('/')[0].startswith('.')}
        core.require(visible == expected, 'Mounted DMG file set or bytes differ')
        result = {'passed': True, 'dmg_sha256': core.sha(dmg), 'files': expected,
                  'readonly_mount': True, 'verified_packaged_pkg_bytes': True}
        core.write(evidence / 'mounted-dmg-test.json', result)
        return result
    finally:
        if attached:
            core.run(['hdiutil', 'detach', mount])


def package(args):
    core.native_arch()
    core.require(args.test_install, 'Actual Apple Installer tests are mandatory')
    core.require(not args.notarize, 'Mac20 unsigned delivery does not support notarization')
    products = core.PRODUCTS
    gill = validate_catalog(products)
    universal = Path(args.universal).resolve()
    report = verify_inputs(universal, args.validations, products)
    core.require(report['signing'] == 'ad-hoc-local-test-only', 'Expected explicitly ad-hoc signed Mac20 build')
    source = Path(args.source).resolve()
    source_report = core.source_check(source)
    core.require(source_report['source_sha256'] == report['source_sha256'], 'Corresponding source differs from binaries')
    archive = Path(args.source_archive).resolve()
    core.require(archive.is_file() and archive.suffix == '.zip', 'Source archive missing')
    core.verify_source_archive(archive, source_report)
    archive_sha = core.sha(archive)
    destination = core.new_destination(args.destination)
    staging = core.new_destination(args.staging)
    core.require_disposable_install_runner(destination / 'probe.pkg')
    for path in (VST3 / 'GILLPRODUCTION', VST3 / 'MOBINA'):
        core.require(not path.exists() or not any(path.iterdir()), 'Disposable runner requires initially empty vendor directories')
    licenses = staging / 'licenses'
    copy_licenses(source, products, licenses)
    outputs = []
    jobs = [(p['name'], [p], False) for p in products] + [('GILL-73-BUNDLE', gill, True)]
    for label, selected, bundle in jobs:
        work = staging / safe_name(label)
        work.mkdir()
        payload = work / 'payload'
        for product in selected:
            shutil.copytree(universal / 'plugins' / (product['name'] + '.vst3'), payload / install_relative(product), symlinks=True)
        docs = payload / 'Library/Application Support' / ('MOBINA' if label == 'MOBINA' else 'GILLPRODUCTION') / 'Mac20' / label
        write_documents(docs, selected, archive, bundle=False, archive_sha=archive_sha)
        shutil.copytree(licenses, docs / 'Lizenzen')
        base = f'{label}-20-MAC-{STATUS}'
        pkg = destination / (base + '.pkg')
        identifier = 'com.gillproduction.mac20.bundle73' if bundle else selected[0]['bundle_id'] + '.mac20.installer'
        make_pkg(payload, selected, work, pkg, identifier)
        evidence = destination / 'verification' / label
        evidence.mkdir(parents=True)
        installation = test_install(pkg, universal, selected, payload, evidence, cleanup=not bundle)
        disk = work / 'disk'
        write_documents(disk, selected, archive, bundle=bundle, archive_sha=archive_sha)
        shutil.copytree(licenses, disk / 'Lizenzen')
        shutil.copy2(pkg, disk / pkg.name)
        if bundle:
            shutil.copy2(archive, disk / archive.name)
        dmg = destination / (base + '.dmg')
        core.run(['hdiutil', 'create', '-volname', label[:27], '-srcfolder', disk, '-format', 'UDZO', dmg])
        mounted = test_dmg(dmg, disk, work / 'mount', evidence)
        outputs.append({'label': label, 'products': [p['name'] for p in selected], 'installer_execution_passed': installation['passed'],
                        'mounted_dmg_passed': mounted['passed'], 'files': {p.name: core.sha(p) for p in (pkg, dmg)}})
        # This directory was created above beneath the checked staging root.
        core.require(work.parent == staging and work.resolve().is_relative_to(staging), 'Unsafe staging cleanup')
        shutil.rmtree(work)
    core.require(len(outputs) == 75, 'Delivery set incomplete')
    result = {'release': '20', 'product_count': len(products), 'gill_bundle_count': len(gill), 'individual_dmg_count': len(products),
              'dmg_count': len(outputs), 'architectures': ['arm64', 'x86_64'], 'minimum_macos': '11.0',
              'notarized': False, 'end_user_release_ready': False, 'signing': 'ad-hoc-local-test-only',
              'installer_and_dmg_signing': 'unsigned', 'fl_studio_tested': False, 'visual_review_required': True,
              'native_validation_passed': True, 'installer_execution_passed': all(o['installer_execution_passed'] for o in outputs),
              'source_sha256': report['source_sha256'], 'source_archive_sha256': archive_sha, 'deliveries': outputs,
              'files': {n: digest for item in outputs for n, digest in item['files'].items()}}
    core.write(destination / 'MAC-RELEASE.json', result)
    return result
