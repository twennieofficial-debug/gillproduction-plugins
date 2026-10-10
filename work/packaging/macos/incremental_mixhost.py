"""Source-identical native reuse for one pinned MIX host UI oracle correction.

Never approves a release. This architecture's original diagnostic ZIP and binary
artifact must be authenticated before reuse. Fresh native hosts are
mandatory; Universal validation and installations remain separate mandatory gates.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import zipfile
import pipeline as p

REPOSITORY = 'twennieofficial-debug/gillproduction-plugins'
BASE_RUN = 38013907475
BASE_COMMIT = '61d37b2e7fcbc3cad371a4b1094b2355f3238e7b'
BASE_SOURCE = 'a566e14257cdb00bfaac0521a45746e129cf8baeec284327a61ce0e816ebd885'
HOST = 'GILLMIX/Tests/RealHost.cpp'
OLD_HOST = 'f2b7099291ad3260ef3ba77e7d9d1fe7780d64d52212530f6dcb48a385b90635'
JOBS = {'arm64': (114099915576, 'native-build (macos-15, arm64)'),
        'x86_64': (114099915536, 'native-build (macos-15-intel, x86_64)')}
MAX_BYTES = 2 * 1024**3
require = p.require


def byte_sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name and '\x00' not in name,
            'Unsafe archive/source name')
    path = PurePosixPath(name.rstrip('/'))
    require(not path.is_absolute() and all(x not in ('', '.', '..') for x in name.rstrip('/').split('/'))
            and path.as_posix() == name.rstrip('/'), 'Unsafe or noncanonical path')
    return path.as_posix()


def approved_host_bytes(original):
    require(byte_sha(original) == OLD_HOST, 'Original host bytes differ')
    s = original.decode('utf-8')
    replacements = [
        ('getWidth() == 760 && editor->getHeight() == 480', 'getWidth() == 780 && editor->getHeight() == 540', 1),
        ('"LEARN / STOP", 501, 452', '"LEARN / STOP", 390, 125', 2),
        ('"CONNECT SELECTED", 650, 80', '"CONNECT SELECTED", 390, 236', 1),
        ('"APPLY", 598, 452', '"APPLY", 335, 164', 1), ('"UNDO", 694, 452', '"UNDO", 446, 164', 1),
        ('            auto window = static_cast<HWND>(editor->getPeer()->getNativeHandle()); RECT r{}; GetClientRect(window, &r);\n'
         '            POINT p{LONG(x * (r.right - r.left) / 760), LONG(y * (r.bottom - r.top) / 480)};',
         '''            auto window = static_cast<HWND>(editor->getPeer()->getNativeHandle()); RECT r{};
            // Coordinates are editor-local logical pixels. Map to this host's
            // own native client area so Windows display scaling is respected.
            if (!GetClientRect(window, &r) || r.right <= r.left || r.bottom <= r.top
                || !editor->getLocalBounds().contains(x, y)) {
                check(false, "native editor client geometry: " + juce::String(label)); return;
            }
            POINT p{MulDiv(x, r.right - r.left, editor->getWidth()),
                    MulDiv(y, r.bottom - r.top, editor->getHeight())};''', 1)]
    for old, new, count in replacements:
        require(s.count(old) == count, 'UI-only transform precondition differs')
        s = s.replace(old, new)
    return s.encode()


def verify_source_closure(base, current, source, original_host, approved_sha):
    require(re.fullmatch('[0-9a-f]{64}', approved_sha or ''), 'Explicit parent-approved host SHA required')
    require(base.get('source_sha256') == BASE_SOURCE, 'Wrong original compiled source')
    for report in (base, current):
        require(report.get('products') == p.PRODUCT_COUNT == 74 and report.get('groups') == p.GROUPS,
                'Product/group set differs')
        files = report.get('files', {})
        require(files and p.digest_manifest(files) == report.get('source_sha256'), 'Source digest differs')
        for name, digest in files.items():
            safe_name(name)
            require(re.fullmatch('[0-9a-f]{40}' if name == 'JUCE_PIN' else '[0-9a-f]{64}', digest), 'Invalid source hash')
    old, new = base['files'], current['files']
    require(set(old) == set(new), 'Source added/removed; FULL_REBUILD required')
    require(old['JUCE_PIN'] == new['JUCE_PIN'] == p.JUCE_COMMIT, 'JUCE changed; FULL_REBUILD required')
    require([name for name in sorted(old) if old[name] != new[name]] == [HOST],
            'Only the approved RealHost UI correction permits reuse; FULL_REBUILD required')
    require(old[HOST] == OLD_HOST and new[HOST] == approved_sha, 'Host hash not approved')
    require(byte_sha(approved_host_bytes(original_host)) == approved_sha, 'Host edit exceeds UI-only correction')
    if source is not None:
        require(approved_host_bytes(original_host) == (Path(source) / HOST).read_bytes(), 'Current host bytes differ')
    for name, digest in new.items():
        if name == 'JUCE_PIN':
            continue
        if source is None:
            continue  # Caller must supply an independently measured current snapshot.
        f = {'PRODUCT_CATALOG': p.HERE / 'products.json', 'CTEST_MATRIX': p.HERE / 'ctest-matrix.json'}.get(name, Path(source) / name)
        require(f.is_file() and p.sha(f) == digest, 'Current compile input missing or changed: ' + name)
    unchanged = {k: v for k, v in old.items() if k != HOST}
    return {'changed_files': [HOST], 'old_host_sha256': OLD_HOST, 'approved_host_sha256': approved_sha,
            'unchanged_files': unchanged, 'unchanged_sha256': p.digest_manifest(unchanged),
            'original_compiled_source': base, 'current_source': current}


def validate_remote_metadata(provenance, arch, require_both=True):
    require(arch in JOBS, 'Unsupported architecture')
    run, jobs, artifacts = provenance['run'], provenance['jobs'], provenance['artifacts']
    require(run.get('id') == BASE_RUN and run.get('head_sha') == BASE_COMMIT and run.get('run_attempt') == 1,
            'Unexpected baseline run/head/attempt')
    require(run.get('repository', {}).get('full_name') == REPOSITORY and run.get('head_repository', {}).get('full_name') == REPOSITORY,
            'Wrong repository')
    require(run.get('event') == 'workflow_dispatch' and run.get('path', '').split('@')[0] == '.github/workflows/build-macos.yml', 'Wrong donor workflow')
    for a in JOBS if require_both else [arch]:
        matches = [j for j in jobs if j.get('name') == JOBS[a][1]]
        require(len(matches) == 1 and matches[0].get('id') == JOBS[a][0] and matches[0].get('run_id') == BASE_RUN
                and matches[0].get('head_sha') == BASE_COMMIT and matches[0].get('status') == 'completed'
                and matches[0].get('conclusion') == 'failure', 'Donor job not complete expected failure: ' + a)
    selected = {}
    names = [f'failed-native-{a}' for a in JOBS] if require_both else [f'failed-native-{arch}']
    names += [f'failed-native-binaries-{arch}']
    for name in names:
        matches = [a for a in artifacts if a.get('name') == name and a.get('expired') is False]
        require(len(matches) == 1, 'Missing/duplicate diagnostic artifact: ' + name)
        a = matches[0]
        require(type(a.get('id')) is int and a['id'] > 0 and type(a.get('size_in_bytes')) is int
                and 0 < a['size_in_bytes'] <= MAX_BYTES and re.fullmatch('sha256:[0-9a-f]{64}', a.get('digest', '')),
                'Invalid artifact transport identity')
        require(a.get('workflow_run', {}).get('id') == BASE_RUN and a['workflow_run'].get('head_sha') == BASE_COMMIT,
                'Artifact run/head differs')
        pinned_ids = {'failed-native-arm64': 11656663433, 'failed-native-binaries-arm64': 11656753556}
        require(name not in pinned_ids or a['id'] == pinned_ids[name], 'Pinned ARM artifact ID differs')
        selected[name] = a
    return selected


def authenticate(path, artifact):
    path = Path(path)
    require(path.is_file() and path.stat().st_size == artifact['size_in_bytes']
            and 'sha256:' + p.sha(path) == artifact['digest'], 'Artifact bytes do not match GitHub transport digest')


def zip_index(z):
    result, seen, size = {}, set(), 0
    for m in z.infolist():
        name = safe_name(m.filename)
        require(name.casefold() not in seen, 'Duplicate archive entry')
        seen.add(name.casefold())
        mode = m.external_attr >> 16
        require(not m.flag_bits & 1 and (stat.S_IFMT(mode) in (0, stat.S_IFREG, stat.S_IFDIR)), 'Encrypted/link/special ZIP entry')
        require(m.file_size >= 0, 'Invalid ZIP size')
        size += m.file_size
        require(size <= MAX_BYTES and len(seen) <= 20000, 'ZIP exceeds bounds')
        if not m.is_dir():
            result[name] = m
    for name in result:
        require(not any(parent.as_posix() in result for parent in PurePosixPath(name).parents if parent.as_posix() != '.'), 'Archive file used as directory')
    return result


def zip_read(z, index, name):
    require(name in index, 'Missing actual evidence: ' + name)
    return z.read(index[name])


def validate_evidence_zip(path, arch):
    """Validate original failure and actual logs; no native success is fabricated."""
    with zipfile.ZipFile(path) as z:
        index = zip_index(z)
        raw = lambda name: zip_read(z, index, name)
        doc = lambda name: json.loads(raw(name))
        base = doc('native-failure.json')
        require(base.get('passed') is False and base.get('architecture') == arch and base.get('failed_groups') == [], 'Donor group failure')
        require(base.get('source', {}).get('source_sha256') == BASE_SOURCE
                and p.digest_manifest(base['source']['files']) == BASE_SOURCE, 'Donor source identity differs')
        require(base['source']['products'] == 74 and base['source']['groups'] == p.GROUPS, 'Donor product source set')
        gates = base.get('failed_gates', [])
        require(len(gates) == 1 and gates[0].get('gate') == 'MixRealHost'
                and gates[0].get('error') == 'Command failed (1); see /Users/runner/work/_temp/native/logs/MixRealHost-48000.txt', 'Different native failure')
        suites = base.get('completed_groups', [])
        require(len(suites) == 23 and {s['group'] for s in suites} == set(p.GROUPS), 'Incomplete 23 groups')
        total = 0
        for s in suites:
            tests = s.get('tests', [])
            require(s.get('passed') is True and len(tests) == len(set(tests)) and set(tests) == set(p.CTEST_MATRIX[s['group']]), 'Different CTest coverage')
            log = raw('logs/' + s['group'] + '-ctest.txt').decode()
            passed = re.findall(r'Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed\s+[0-9.]+\s+sec', log)
            require(len(passed) == len(tests) and set(passed) == set(tests)
                    and re.search(r'100% tests passed(?:, 0 tests failed)? out of '+str(len(tests))+r'\b', log)
                    and not re.search(r'\*\*\*|\b(?:Failed|Timeout|SEGFAULT)\b', log), 'Actual CTest log differs: ' + s['group'])
            raw('logs/' + s['group'] + '-LastTest.log')
            total += len(tests)
        require(total == 89, 'Not exactly 89 native tests')
        quality = doc('test-evidence/QUALITY_HOST/quality-host-gate.json')
        products = [{'name': n, 'bundle_sha256': h} for n, h in quality.get('bundle_sha256', {}).items()]
        p.verify_native_quality_gate({'architecture': arch, 'source': base['source'], 'plugins': products, 'quality_host': quality})
        for r in quality['runs']:
            expected = f'test-evidence/QUALITY_HOST/quality-host-{r["sample_rate"]}.json'
            require(r['report'] == expected and byte_sha(raw(expected)) == r['report_sha256'] and doc(expected) == r['result'], 'Old quality report bytes differ')
            require(r['result']['checks'] == 2690, 'Actual QualityHost checks differ')
            raw(f'logs/QualityHost-{r["sample_rate"]}.txt')
        mix = doc('test-evidence/MIX_HOST/mix-host-48000.json')
        require(mix.get('passed') is False and mix.get('checks') == 183 and mix.get('failures') == 1, 'Different actual old MIX failure')
        failed_mix_gate = doc('test-evidence/MIX_HOST/mix-host-gate.json')
        require(failed_mix_gate.get('passed') is False and failed_mix_gate.get('architecture') == arch
                and failed_mix_gate.get('source_sha256') == BASE_SOURCE, 'Original failed MIX gate identity differs')
        identity_only = copy.deepcopy(mix)
        identity_only.update(passed=True, failures=0)
        # Temporary validation view checks identities only; original is never modified or emitted as passing.
        p.verify_mix_result(identity_only)
        for item in mix['products']:
            require(item['bundle'] == '/Users/runner/work/_temp/native/plugins/' + item['name'] + '.vst3', 'Unexpected original host bundle')
        log = raw('logs/MixRealHost-48000.txt').decode()
        require(re.findall(r'^FAIL .*$', log, re.M) == ['FAIL real hosted compact controller dimensions']
                and re.findall(r'^RESULT .*$', log, re.M) == ['RESULT 184 checks, 1 failures'], 'Old failure is not solely compact geometry')
        for label in ('CONNECT SELECTED', 'LEARN / STOP', 'APPLY', 'UNDO'):
            require(f'APPKIT accessibilityPerformPress {label}: accepted' in log, 'Missing native AppKit action ' + label)
        require('STAGE 7' in log, 'Incomplete native host flow')
        ui = []
        for product in p.PRODUCTS:
            prefix = 'test-evidence/' + product['group'] + '/' + product['name'] + '-UI-'
            matches = []
            for name in index:
                if name.startswith(prefix) and name.endswith('.png'):
                    data = raw(name)
                    if len(data) >= 24 and data[:8] == b'\x89PNG\r\n\x1a\n' and list(p.struct.unpack('>II', data[16:24])) == product['default_size']:
                        matches.append(name)
            require(matches, 'Missing native compact UI screenshot: ' + product['name'])
            ui.append({'name': product['name'], 'size': product['default_size'], 'screenshots': sorted(matches)})
        fingerprints = {name: byte_sha(raw(name)) for name in sorted(index)}
        return {'base': base, 'quality': quality, 'mix': mix, 'ui': ui, 'evidence_files': fingerprints,
                'evidence_manifest_sha256': p.digest_manifest(fingerprints), 'ctest_count': total}


def extract_zip(path, destination):
    with zipfile.ZipFile(path) as z:
        index = zip_index(z)
        p.new_destination(destination)
        for name, m in index.items():
            target = Path(destination) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(m) as incoming, target.open('xb') as outgoing:
                shutil.copyfileobj(incoming, outgoing)


def unpack_binary_zip(path, destination, arch):
    """Deny every link/special entry; preserve all regular bytes and POSIX modes."""
    destination = p.new_destination(destination)
    with zipfile.ZipFile(path) as z:
        index = zip_index(z)
        require(set(index) == {f'failed-native-binaries-{arch}.tar.gz'}, 'Unexpected binary artifact payload')
        tarpath = destination / 'payload.tar.gz'
        with z.open(next(iter(index.values()))) as incoming, tarpath.open('xb') as outgoing:
            shutil.copyfileobj(incoming, outgoing)
    root = destination / 'unpacked'
    root.mkdir()
    manifest, seen, size = {}, set(), 0
    with tarfile.open(tarpath, 'r:gz') as tar:
        members = tar.getmembers()
        for m in members:
            name = safe_name(m.name)
            require(name.casefold() not in seen, 'Duplicate TAR entry')
            seen.add(name.casefold())
            require(m.isfile() or m.isdir(), 'TAR links/special entries forbidden')
            require(not m.mode & ~0o777, 'Unsafe POSIX mode')
            size += m.size
            require(0 <= m.size <= MAX_BYTES and size <= MAX_BYTES and len(seen) <= 20000, 'TAR exceeds bounds')
            require(name == 'diagnostic-binaries.json' or name == 'native/native-failure.json'
                    or name == 'native/plugins' or name.startswith('native/plugins/')
                    or name == 'native/test-evidence/QUALITY_HOST' or name.startswith('native/test-evidence/QUALITY_HOST/')
                    or name == 'gill-build/QualityHost/GillQualityHost', 'Unexpected binary TAR entry: ' + name)
            manifest[name] = {'kind': 'file' if m.isfile() else 'directory', 'mode': m.mode, 'size': m.size}
        for name in manifest:
            for parent in PurePosixPath(name).parents:
                require(parent.as_posix() not in manifest or manifest[parent.as_posix()]['kind'] == 'directory', 'TAR parent is a file')
        for m in members:
            name = safe_name(m.name)
            target = root / name
            if m.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(m) as incoming, target.open('xb') as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
                manifest[name]['sha256'] = p.sha(target)
            os.chmod(target, m.mode)
    metadata = p.read(root / 'diagnostic-binaries.json')
    require(metadata.get('diagnostic_only') is True and metadata.get('release_approved') is False
            and metadata.get('architecture') == arch and metadata.get('source_commit') == BASE_COMMIT
            and metadata.get('run_id') == str(BASE_RUN) and metadata.get('run_attempt') == '1', 'Binary donor provenance differs')
    require(metadata.get('plugins') == sorted(x['name'] + '.vst3' for x in p.PRODUCTS), 'Binary donor plugin catalog differs')
    require(metadata.get('payload_bytes') == sum(r['size'] for n, r in manifest.items() if r['kind'] == 'file' and n != 'diagnostic-binaries.json'), 'Payload byte count differs')
    return root, {'native_tar_sha256': p.sha(tarpath), 'entries': manifest, 'manifest_sha256': p.digest_manifest(manifest), 'metadata': metadata}


def verify_payload(root, evidence, arch):
    plugins = root / 'native/plugins'
    require(sorted(path.name for path in plugins.iterdir()) == sorted(x['name'] + '.vst3' for x in p.PRODUCTS)
            and all(path.is_dir() and not path.is_symlink() for path in plugins.iterdir()), 'Unexpected original native plugin entry')
    require(p.sha(root / 'native/native-failure.json') == evidence['evidence_files']['native-failure.json'], 'Binary/evidence failure reports differ')
    quality = evidence['quality']
    require(p.sha(root / 'gill-build/QualityHost/GillQualityHost') == quality['executable_sha256'], 'Original native host bytes differ')
    records = [p.inspect_bundle(root / 'native/plugins' / (x['name'] + '.vst3'), x, [arch]) for x in p.PRODUCTS]
    require({r['name']: r['bundle_sha256'] for r in records} == quality['bundle_sha256'], 'Plugin bytes/modes differ from actual native QualityHost')
    for name, digest in evidence['evidence_files'].items():
        if name.startswith('test-evidence/QUALITY_HOST/'):
            require(p.sha(root / 'native' / name) == digest, 'Binary/evidence QualityHost evidence differs')
    return records


def execute_hosts(source, build_root, dest, arch, jobs, records, snapshot):
    """No injectable success flags; production always invokes both actual helpers."""
    quality = p.build_quality_host(source, build_root, dest, arch, jobs, records, snapshot['source_sha256'])
    mix = p.build_mix_host(source, build_root, dest, arch, jobs, records, snapshot['source_sha256'])
    report = {'architecture': arch, 'source': snapshot, 'plugins': records, 'quality_host': quality, 'mix_host': mix}
    p.verify_native_quality_gate(report, dest)
    p.verify_native_mix_gate(report, dest)
    require(all(r['result']['checks'] == 2690 for r in quality['runs']), 'Fresh four-rate quality check count differs')
    require(mix['result']['checks'] == 183 and mix['result']['failures'] == 0, 'Fresh Mac host check count differs')
    log = (dest / 'logs/MixRealHost-48000.txt').read_text()
    require(re.findall(r'^RESULT .*$', log, re.M) == ['RESULT 184 checks, 0 failures']
            and not re.search(r'^FAIL ', log, re.M), 'Fresh native host stdout differs')
    return quality, mix


def audit_binary_tar(tarpath, evidence, arch):
    """Offline cryptographic byte/mode inspection, never a native execution claim."""
    entries, small, total = {}, {}, 0
    with tarfile.open(tarpath, 'r:gz') as tar:
        for m in tar:
            name = safe_name(m.name)
            require(name.casefold() not in {n.casefold() for n in entries}, 'Duplicate TAR entry')
            require((m.isfile() or m.isdir()) and not m.mode & ~0o777, 'Link/special/unsafe mode in TAR')
            require(name == 'diagnostic-binaries.json' or name == 'native/native-failure.json'
                    or name == 'native/plugins' or name.startswith('native/plugins/')
                    or name == 'native/test-evidence/QUALITY_HOST' or name.startswith('native/test-evidence/QUALITY_HOST/')
                    or name == 'gill-build/QualityHost/GillQualityHost', 'Unexpected TAR member')
            total += m.size
            require(0 <= m.size <= MAX_BYTES and total <= MAX_BYTES and len(entries) < 20000, 'TAR size bound')
            record = {'kind': 'file' if m.isfile() else 'directory', 'mode': m.mode, 'size': m.size}
            if m.isfile():
                with tar.extractfile(m) as stream:
                    prefix = stream.read(32)
                    digest = hashlib.sha256(prefix)
                    collect = name.endswith('.json') or name.endswith('/Info.plist')
                    data = bytearray(prefix) if collect else None
                    while chunk := stream.read(1024 * 1024):
                        digest.update(chunk)
                        if collect:
                            require(len(data) + len(chunk) <= 4 * 1024**2, 'Metadata bound')
                            data.extend(chunk)
                    record['sha256'] = digest.hexdigest()
                    if collect:
                        small[name] = bytes(data)
                    if '/Contents/MacOS/' in name or name == 'gill-build/QualityHost/GillQualityHost':
                        require(prefix[:4] == b'\xcf\xfa\xed\xfe' and len(prefix) >= 8
                                and int.from_bytes(prefix[4:8], 'little') == {'arm64': 0x100000c, 'x86_64': 0x1000007}[arch]
                                and m.mode & 0o111, 'Native thin executable architecture/mode mismatch')
            entries[name] = record
    for name in entries:
        require(all(parent.as_posix() not in entries or entries[parent.as_posix()]['kind'] == 'directory'
                    for parent in PurePosixPath(name).parents), 'TAR file used as parent')
    meta = json.loads(small['diagnostic-binaries.json'])
    require(meta.get('diagnostic_only') is True and meta.get('release_approved') is False
            and meta.get('architecture') == arch and meta.get('source_commit') == BASE_COMMIT
            and meta.get('run_id') == str(BASE_RUN) and meta.get('run_attempt') == '1', 'Binary metadata identity')
    require(meta.get('payload_bytes') == total - entries['diagnostic-binaries.json']['size'], 'Binary byte total differs')
    names = sorted(x['name'] + '.vst3' for x in p.PRODUCTS)
    actual = sorted({name.split('/')[2] for name in entries if name.startswith('native/plugins/')})
    require(meta.get('plugins') == actual == names, 'Exact original 74 native bundles required')
    require(entries['native/native-failure.json']['sha256'] == evidence['evidence_files']['native-failure.json'], 'Original failure report mismatch')
    require(entries['gill-build/QualityHost/GillQualityHost']['sha256'] == evidence['quality']['executable_sha256'], 'Original host executable mismatch')
    for name, digest in evidence['evidence_files'].items():
        if name.startswith('test-evidence/QUALITY_HOST/'):
            require(entries['native/' + name]['sha256'] == digest, 'Original quality evidence differs')
    bundles = {}
    for product in p.PRODUCTS:
        prefix = 'native/plugins/' + product['name'] + '.vst3/'
        files = {n[len(prefix):]: {'sha256': r['sha256'], 'executable': bool(r['mode'] & 0o111)}
                 for n, r in entries.items() if n.startswith(prefix) and r['kind'] == 'file'}
        info = p.plistlib.loads(small[prefix + 'Contents/Info.plist'])
        require(info.get('CFBundleIdentifier') == product['bundle_id'] and info.get('CFBundleVersion') == product['version']
                and info.get('CFBundleShortVersionString') == product['version'], 'Original bundle ID/version mismatch')
        require('Contents/MacOS/' + info['CFBundleExecutable'] in files, 'Missing original native executable')
        bundles[product['name']] = p.digest_manifest(files)
    require(bundles == evidence['quality']['bundle_sha256'], 'Original native plugin bytes/modes differ from measured quality evidence')
    return {'native_tar_sha256': p.sha(tarpath), 'entries': entries, 'manifest_sha256': p.digest_manifest(entries),
            'metadata': meta, 'bundles': bundles, 'native_execution_performed': False}


def gh_json(endpoint):
    return json.loads(subprocess.check_output(['gh', 'api', endpoint], timeout=120))


def fresh_provenance():
    endpoint = f'repos/{REPOSITORY}/actions/runs/{BASE_RUN}'
    return {'run': gh_json(endpoint), 'jobs': gh_json(endpoint + '/jobs?per_page=100')['jobs'],
            'artifacts': gh_json(endpoint + '/artifacts?per_page=100')['artifacts']}


def normalize_provenance(value):
    return {'run': value['run'], 'jobs': value.get('jobs', [value.get('job')]),
            'artifacts': list(value['artifacts'].values()) if isinstance(value['artifacts'], dict) else value['artifacts']}


def build(args):
    arch = p.native_arch()
    source = Path(args.source).resolve()
    require(Path(p.__file__).resolve() == (source.parent / 'work/packaging/macos/pipeline.py').resolve(), 'Imported build helpers differ from source checkout')
    snapshot = p.source_check(source)
    provenance = fresh_provenance()  # Do not trust supplied metadata as authorization.
    selected = validate_remote_metadata(provenance, arch, require_both=False)
    supplied = validate_remote_metadata(normalize_provenance(p.read(args.provenance)), arch, require_both=False)
    require(selected == supplied, 'Supplied provenance differs from current GitHub artifact metadata')
    authenticate(args.diagnostic_zip, selected[f'failed-native-{arch}'])
    authenticate(args.binary_zip, selected[f'failed-native-binaries-{arch}'])
    evidence = {arch: validate_evidence_zip(args.diagnostic_zip, arch)}
    original = (Path(args.original_host).read_bytes() if args.original_host else
                subprocess.check_output(['git', '-C', str(source.parent), 'show', BASE_COMMIT + ':work/' + HOST], timeout=30))
    proof = verify_source_closure(evidence[arch]['base']['source'], snapshot, source, original, args.approved_host_sha256)
    # Build helpers themselves must be the donor recipes: a new module must not silently change their behavior.
    recipes = {}
    for relative in ('work/packaging/macos/pipeline.py', 'work/packaging/macos/products.json', 'work/packaging/macos/ctest-matrix.json'):
        old = subprocess.check_output(['git', '-C', str(source.parent), 'show', BASE_COMMIT + ':' + relative], timeout=30)
        require(old == (source.parent / relative).read_bytes(), 'Native recipe changed; FULL_REBUILD required: ' + relative)
        recipes[relative] = byte_sha(old)
    work = p.new_destination(args.work_root or str(args.destination) + '-reuse-work')
    binary_root, archive = unpack_binary_zip(args.binary_zip, work / 'binary', arch)
    records = verify_payload(binary_root, evidence[arch], arch)
    dest = p.new_destination(args.destination)
    build_root = p.new_destination(args.build_root)
    baseline = dest / 'reuse-baseline'
    extract_zip(args.diagnostic_zip, baseline)
    p.write(baseline / 'transport-provenance.json', provenance)
    p.write(baseline / 'binary-archive-manifest.json', archive)
    p.write(baseline / 'source-equality.json', proof)
    shutil.copytree(binary_root / 'native/plugins', dest / 'plugins', symlinks=False)
    for group in p.GROUPS:
        shutil.copytree(baseline / 'test-evidence' / group, dest / 'test-evidence' / group)
        for file in (baseline / 'logs').glob(group + '-*'):
            if file.is_file():
                target = dest / 'logs' / file.relative_to(baseline / 'logs')
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, target)
    before = {x['name']: p.tree_manifest(dest / 'plugins' / (x['name'] + '.vst3')) for x in p.PRODUCTS}
    for x in p.PRODUCTS:
        incoming = binary_root / 'native/plugins' / (x['name'] + '.vst3')
        outgoing = dest / 'plugins' / (x['name'] + '.vst3')
        require(p.tree_manifest(incoming) == before[x['name']], 'Copy changed original plugin bytes/modes')
        for name in archive['entries']:
            prefix = 'native/plugins/' + x['name'] + '.vst3/'
            if name.startswith(prefix):
                target = outgoing / name[len(prefix):]
                require(stat.S_IMODE(target.stat().st_mode) == archive['entries'][name]['mode'], 'POSIX mode changed')
    quality, mix = execute_hosts(source, build_root, dest, arch, args.jobs, records, snapshot)
    (dest / 'reuse-hosts').mkdir()
    for folder, exe, gate in [('QualityHost', 'GillQualityHost', quality), ('MixRealHost', 'GillMixRealHost', mix)]:
        shutil.copy2(build_root / folder / exe, dest / 'reuse-hosts' / exe)
        require(p.sha(dest / 'reuse-hosts' / exe) == gate['executable_sha256'], 'Fresh host preservation differs')
    require(p.source_check(source) == snapshot, 'Source changed during host execution')
    for x in p.PRODUCTS:
        require(p.tree_manifest(dest / 'plugins' / (x['name'] + '.vst3')) == before[x['name']], 'Host altered original bundles')
    for name, item in archive['entries'].items():
        if name.startswith('native/plugins/'):
            target = dest / name[len('native/'):]
            require(stat.S_IMODE(target.stat().st_mode) == item['mode'], 'Host altered original POSIX mode')
    for name, digest in evidence[arch]['evidence_files'].items():
        require(p.sha(baseline / name) == digest, 'Original failed evidence changed during execution')
    suites = copy.deepcopy(evidence[arch]['base']['completed_groups'])
    for s in suites:
        s.update(execution='reused-source-identical', original_run_id=BASE_RUN, original_compiled_source_sha256=BASE_SOURCE)
    report = {'passed': True, 'architecture': arch, 'source': snapshot, 'native_ctest': suites,
              'compact_ui_dimensions': evidence[arch]['ui'], 'plugins': records, 'quality_host': quality, 'mix_host': mix,
              'native_build_method': 'source_identical_reuse', 'source_identical_reuse': {
                  'baseline_run_id': BASE_RUN, 'baseline_commit': BASE_COMMIT, 'baseline_source_sha256': BASE_SOURCE,
                  'original_failure_report_sha256': evidence[arch]['evidence_files']['native-failure.json'],
                  'baseline_evidence_manifest': evidence[arch]['evidence_files'],
                  'baseline_evidence_manifest_sha256': evidence[arch]['evidence_manifest_sha256'],
                  'binary_archive_manifest_sha256': archive['manifest_sha256'], 'source_equality': proof,
                  'identical_recipe_sha256': recipes,
                  'artifacts': selected, 'fresh_native_hosts': ['QualityHost', 'MixRealHost'],
                  'release_approved': False}, 'fl_studio_tested': False, 'visual_review_required': True}
    p.write(dest / 'native-build.json', report)
    return report


def transported_native_manifest(archive, evidence_root):
    """Read native modes from original TAR and bind every extracted byte to it."""
    entries, total = {}, 0
    root = Path(evidence_root)
    with tarfile.open(archive, 'r:gz') as tar:
        for member in tar:
            name = member.name
            if name in ('.', './'):
                require(member.isdir(), 'Invalid TAR root'); continue
            if name.startswith('./'):
                name = name[2:]
            name = safe_name(name)
            require(name.casefold() not in {n.casefold() for n in entries}, 'Duplicate native TAR member')
            require((member.isfile() or member.isdir()) and not member.mode & ~0o777, 'Native TAR links/special/mode')
            total += member.size
            require(0 <= member.size <= 6 * 1024**3 and total <= 6 * 1024**3 and len(entries) < 50000, 'Native TAR bounds')
            record = {'kind': 'file' if member.isfile() else 'directory', 'mode': member.mode, 'size': member.size}
            if member.isfile():
                with tar.extractfile(member) as stream:
                    record['sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
                path = root / name
                require(path.is_file() and not path.is_symlink() and path.stat().st_size == member.size
                        and p.sha(path) == record['sha256'], 'Transported native file differs: ' + name)
            entries[name] = record
    return entries


def require_thin_arch(path, arch):
    with Path(path).open('rb') as stream:
        header = stream.read(8)
    require(header[:4] == b'\xcf\xfa\xed\xfe' and len(header) == 8
            and int.from_bytes(header[4:], 'little') == {'arm64': 0x100000c, 'x86_64': 0x1000007}[arch],
            'Actual native executable architecture differs')


def verify_reuse_evidence(report, evidence_root, current_source, *, original_diagnostic_zip,
                          original_binary_tar, approved_host_sha256, original_host, final_native_tar=None):
    """Independent native-POSIX re-verification; original archives remain required.

    A Windows consumer must inspect the outer native TAR modes rather than claim
    this filesystem check passed after ZIP/Windows extraction changed those modes.
    """
    root = Path(evidence_root)
    source = None if isinstance(current_source, dict) else Path(current_source)
    arch = report.get('architecture')
    require(arch in JOBS and report.get('passed') is True and report.get('native_build_method') == 'source_identical_reuse', 'Wrong reuse report method')
    reuse = report.get('source_identical_reuse', {})
    require(reuse.get('baseline_run_id') == BASE_RUN and reuse.get('baseline_commit') == BASE_COMMIT
            and reuse.get('baseline_source_sha256') == BASE_SOURCE and reuse.get('release_approved') is False, 'Wrong reuse baseline')
    authenticate(original_diagnostic_zip, reuse['artifacts'][f'failed-native-{arch}'])
    old = validate_evidence_zip(original_diagnostic_zip, arch)
    snapshot = current_source if isinstance(current_source, dict) else p.source_check(source)
    proof = verify_source_closure(old['base']['source'], snapshot, source, original_host, approved_host_sha256)
    require(report.get('source') == snapshot and reuse.get('source_equality') == proof, 'Rebased source proof differs')
    require(reuse.get('original_failure_report_sha256') == old['evidence_files']['native-failure.json']
            and reuse.get('baseline_evidence_manifest') == old['evidence_files']
            and reuse.get('baseline_evidence_manifest_sha256') == old['evidence_manifest_sha256'], 'Original evidence manifest differs')
    archive = audit_binary_tar(original_binary_tar, old, arch)
    stored = p.read(root / 'reuse-baseline/binary-archive-manifest.json')
    require(stored['entries'] == archive['entries'] and stored['native_tar_sha256'] == archive['native_tar_sha256']
            and stored['manifest_sha256'] == archive['manifest_sha256'] == reuse.get('binary_archive_manifest_sha256'), 'Binary archive provenance differs')
    for name, digest in old['evidence_files'].items():
        require(p.sha(root / 'reuse-baseline' / name) == digest, 'Original failure evidence was altered')
        if name.startswith('test-evidence/') and not name.startswith(('test-evidence/QUALITY_HOST/', 'test-evidence/MIX_HOST/')):
            require(p.sha(root / name) == digest, 'Preserved native UI/source evidence differs')
    require(report.get('compact_ui_dimensions') == old['ui'], 'Native UI record differs')
    suites = copy.deepcopy(old['base']['completed_groups'])
    for suite in suites:
        suite.update(execution='reused-source-identical', original_run_id=BASE_RUN, original_compiled_source_sha256=BASE_SOURCE)
        for suffix in ('ctest.txt', 'LastTest.log'):
            name = 'logs/' + suite['group'] + '-' + suffix
            require(p.sha(root / name) == old['evidence_files'][name], 'Actual reused test log differs')
    require(report.get('native_ctest') == suites, 'Reused native CTest provenance differs')
    transported = transported_native_manifest(final_native_tar, root) if final_native_tar else None
    if transported is not None:
        require(p.read(root / 'native-build.json') == report and 'native-build.json' in transported, 'Transported native report differs')
        records = report.get('plugins', [])
        require(len(records) == 74 and {r['name'] for r in records} == {x['name'] for x in p.PRODUCTS}, 'Transported plugin inventory')
        for product in p.PRODUCTS:
            record = next(r for r in records if r['name'] == product['name'])
            prefix = 'plugins/' + product['name'] + '.vst3/'
            files = {n[len(prefix):]: {'sha256': r['sha256'], 'executable': bool(r['mode'] & 0o111)}
                     for n, r in transported.items() if n.startswith(prefix) and r['kind'] == 'file'}
            require(p.digest_manifest(files) == archive['bundles'][product['name']] == record['bundle_sha256'], 'Transported plugin bytes/modes differ')
            require(record['version'] == product['version'] and record['bundle_id'] == product['bundle_id'], 'Transported identity differs')
            require(record.get('mach_o'), 'Missing native executable identity')
            actual_macho = {relative for relative in files if p.macho(root / prefix / relative)}
            require({code['path'] for code in record['mach_o']} == actual_macho, 'Native code inventory differs')
            for code in record['mach_o']:
                require(code['architectures'] == [arch] and files[code['path']]['sha256'] == code['sha256'], 'Transported Mach-O identity differs')
                require_thin_arch(root / prefix / code['path'], arch)
    else:
        records = [p.inspect_bundle(root / 'plugins' / (x['name'] + '.vst3'), x, [arch]) for x in p.PRODUCTS]
        require(records == report.get('plugins') and {r['name']: r['bundle_sha256'] for r in records} == archive['bundles'], 'Final native bundle bytes differ')
    for name, item in archive['entries'].items():
        if name.startswith('native/plugins/'):
            relative = name[len('native/'):]
            mode = transported[relative]['mode'] if transported is not None else stat.S_IMODE((root / relative).stat().st_mode)
            require(mode == item['mode'], 'Native POSIX mode differs')
    p.verify_native_quality_gate(report, root)
    p.verify_native_mix_gate(report, root)
    require(all(r['result']['checks'] == 2690 for r in report['quality_host']['runs']), 'Fresh quality count differs')
    for name, gate in [('GillQualityHost', report['quality_host']), ('GillMixRealHost', report['mix_host'])]:
        require(p.sha(root / 'reuse-hosts' / name) == gate['executable_sha256'] and p.macho(root / 'reuse-hosts' / name), 'Fresh actual native host executable missing')
        require_thin_arch(root / 'reuse-hosts' / name, arch)
        mode = transported['reuse-hosts/' + name]['mode'] if transported is not None else (root / 'reuse-hosts' / name).stat().st_mode
        require(mode & 0o111, 'Fresh native host lacks executable permission')
    require(report['mix_host']['result']['checks'] == 183 and report['mix_host']['result']['failures'] == 0, 'Fresh MIX report check count')
    require('RESULT 184 checks, 0 failures' in (root / 'logs/MixRealHost-48000.txt').read_text(), 'Fresh MIX stdout missing')
    return {'verified': True, 'baseline_tests_reused': 89, 'products_unchanged': 74,
            'fresh_quality_rates': list(p.QUALITY_SAMPLE_RATES), 'release_approved': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('source', 'provenance', 'diagnostic-zip', 'binary-zip',
                 'approved-host-sha256', 'build-root', 'destination'):
        parser.add_argument('--' + flag, required=True)
    parser.add_argument('--original-host')
    parser.add_argument('--work-root')
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    require(1 <= args.jobs <= 8, 'Invalid native build parallelism')
    build(args)


if __name__ == '__main__':
    main()
