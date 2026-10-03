"""Exact-file release 06 evidence; no live installation or implicit publishing."""
from pathlib import Path
from datetime import datetime, timezone
import concurrent.futures
import hashlib
import importlib.util
import json
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / 'work'
BASE = Path('E:/GILLPRODUCTION/Development06')
PACKAGE = BASE / 'Packaging'
CATALOG = json.loads((WORK / 'packaging/macos/products.json').read_text(encoding='utf-8'))
NEW = {'GILLCONTROL': 'Control', 'GILLCREATIVE': 'Creative', 'GILLSMARTDEESSER': 'SmartDeEsser'}

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def build_root(group):
    return BASE / NEW[group] if group in NEW else BASE / 'Existing' / group

def bundle(product):
    return build_root(product['group']) / (product['target'] + '_artefacts/Release/VST3') / (product['name'] + '.vst3')

def module(product):
    return bundle(product) / 'Contents/x86_64-win' / (product['name'] + '.vst3')

def gates():
    spec = importlib.util.spec_from_file_location('release06_source_gate', WORK / 'packaging/macos/pipeline.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

def validate(product):
    name = product['name']
    binary = module(product)
    before = sha(binary)
    dest = BASE / 'Validation' / product['target']
    dest.mkdir(parents=True, exist_ok=True)
    resultpath = dest / 'result.json'
    if resultpath.exists():
        old = read(resultpath)
        if old.get('passed') and old['module_sha256'] == before and old['version'] == product['version']:
            print('UNCHANGED VALIDATED ' + name, flush=True)
            return True
    args = [str(WORK / 'toolchain/pluginval/pluginval.exe'), '--strictness-level', '10',
            '--random-seed', '20260925', '--sample-rates', '44100,48000,88200,96000,192000',
            '--block-sizes', '1,16,64,127,256,512,1024,2048', '--timeout-ms', '120000',
            '--output-dir', str(dest), '--output-filename', 'validation.txt', '--validate', str(bundle(product))]
    print('VALIDATING ' + name, flush=True)
    try:
        with (dest / 'console.txt').open('wb') as log:
            code = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=1800).returncode
    except subprocess.TimeoutExpired:
        code = -999
    console = (dest / 'console.txt').read_text(encoding='utf-8', errors='replace')
    factory = name + ' v' + product['version'] in console
    unchanged = sha(binary) == before
    result = {'name': name, 'version': product['version'], 'validator': 'Tracktion pluginval 1.0.4',
              'strictness': 10, 'random_seed': 20260925, 'sample_rates': [44100,48000,88200,96000,192000],
              'block_sizes': [1,16,64,127,256,512,1024,2048], 'module_sha256': before,
              'module_unchanged': unchanged, 'exit_code': code, 'factory_version_verified': factory,
              'passed': code == 0 and unchanged and factory, 'completed_at': datetime.now(timezone.utc).isoformat()}
    write(resultpath, result)
    print(('PASS ' if result['passed'] else 'FAIL ') + name, flush=True)
    if not result['passed']:
        print(console[-9000:], flush=True)
    return result['passed']

def host_list():
    paths = [str(bundle(p)) for p in CATALOG]
    assert all(Path(p).is_dir() for p in paths)
    write(BASE / 'QualityHost/all34.json', paths)
    print(BASE / 'QualityHost/all34.json')

def manifest():
    gate = gates()
    source = gate.source_check(WORK)
    native = []
    for group in gate.GROUPS:
        xml = build_root(group) / 'ctest.xml'
        suite = ET.parse(xml).getroot()
        tests = [{'name': t.attrib['name'], 'passed': t.attrib.get('status') == 'run' and t.find('failure') is None and t.find('skipped') is None} for t in suite.findall('testcase')]
        assert tests and all(t['passed'] for t in tests), group
        gate.verify_ctest_listing(group, {'tests': tests})
        native.append({'group': group, 'passed': True, 'tests': tests, 'report': str(xml), 'report_sha256': sha(xml)})
    host = []
    for rate in (44100,48000,96000,192000):
        path = BASE / 'QualityHost' / ('all34-' + str(rate) + '.json')
        report = read(path)
        assert report['passed'] and report['failures'] == 0 and report['actual_vst3_bundles'] == len(CATALOG), str(path)
        assert all(p['factory_version_verified'] for p in report['products'])
        host.append({'sample_rate': rate, 'path': str(path), 'sha256': sha(path), 'checks': report['checks']})
    rows = []
    for p in CATALOG:
        check = read(BASE / 'Validation' / p['target'] / 'result.json')
        assert check['passed'] and check['module_sha256'] == sha(module(p)), p['name']
        rows.append({'name': p['name'], 'group': p['group'], 'target': p['target'], 'version': p['version'],
                     'plugin_code': p['code'], 'bundle': str(bundle(p)), 'module': str(module(p)),
                     'sha256': sha(module(p)), 'validation': check})
    archive = PACKAGE / 'Delivery/GILL-UPDATE-06-QUELLCODE.zip'
    gate.verify_source_archive(archive, source)
    proof = {'schema': 1, 'release': '06', 'passed': True, 'source_sha256': source['source_sha256'],
             'created_at': datetime.now(timezone.utc).isoformat(), 'native_ctest': native, 'real_vst3_host': host,
             'products': rows, 'source_archive': {'path': str(archive), 'sha256': sha(archive), 'source_sha256': source['source_sha256']}}
    write(PACKAGE / 'windows-validation.json', proof)
    print('VERIFIED ' + str(len(rows)) + ' products and ' + str(sum(len(g['tests']) for g in native)) + ' native suites')

if __name__ == '__main__':
    action = sys.argv[1]
    if action == 'validate':
        products = [p for p in CATALOG if not sys.argv[2:] or p['target'] in sys.argv[2:]]
        assert products
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(validate, products))
        sys.exit(0 if all(results) else 1)
    elif action == 'host-list':
        host_list()
    elif action == 'manifest':
        manifest()
    else:
        raise SystemExit('Expected validate, host-list or manifest')
