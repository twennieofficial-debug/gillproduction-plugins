#!/usr/bin/env python3
"""Update22 regression gate. No production source mutation; native Mac proof only on Mac."""
import argparse
import array
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
PRODUCTS = ['GILLMULTI', 'GILLPRECISION', 'GILLDEESSPRO', 'GILLALPHA', 'GILLRACK', 'GILLSPECTRA PREVIEW']
CASES = [
    ('spectral-baseline', 1, 'LIVE selected noise unaffected'),
    ('spectral-current', 0, 'SPECTRAL_AUDIO_TESTS_OK'),
    ('noise-controls', 0, 'NOISE_CONTROLS_PASS'),
    ('level', 0, 'SPECTRA_LEVEL_AUDIO_PASS'),
    ('advanced', 0, 'SPECTRA_ADVANCED_AUDIO_OK'),
    ('mono-current', 0, 'SPECTRAL_MONO_KEY_PASS'),
    ('mono-baseline', 1, 'anti-phase stereo detector cancelled'),
    ('frozen-regression', 0, 'FROZEN_UNCHANGED_PATHS_REGRESSION_PASS'),
    ('SuiteCore', 0, 'STUDIO_CORE_AUDIO_PASS'),
    ('SuiteMechanisms', 0, 'STUDIO_MECHANISMS_PASS'),
]

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def read(p):
    return json.loads(Path(p).read_text(encoding='utf-8'))

def write(p, value):
    Path(p).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')

def tree(p):
    return {f.relative_to(p).as_posix(): sha(f) for f in sorted(Path(p).rglob('*')) if f.is_file()}

def bundle_tree(root, transported=None):
    root=Path(root).resolve();result={}
    for path in sorted(root.rglob('*')):
        rel=path.relative_to(root).as_posix()
        if path.is_symlink():
            require(path.resolve().is_relative_to(root),'Bundle symlink escapes')
            result[rel]={'symlink':os.readlink(path)}
        elif path.is_file():
            result[rel]={'sha256':sha(path),'executable':bool(path.stat().st_mode & 0o111)}
    if transported is not None:
        require(set(result)==set(transported),'Transported bundle file set differs')
        for name,actual in result.items():
            expected=transported[name]
            if 'symlink' in actual:require(actual==expected,'Transported symlink differs')
            else:
                require(set(expected)=={'sha256','executable'} and type(expected['executable']) is bool and actual['sha256']==expected['sha256'],'Transported bundle content differs')
        return transported
    return result

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def binding(source):
    selected = {}
    for group in ['GILLSTUDIO/Suite/Source', 'GILLSTUDIO/Suite/Tests', 'GILLSTUDIO/Next03/Source', 'GILLSTUDIO/Next03/Tests/Spectral', 'GILLSTUDIO/Source', 'GILLCommon', 'GILLMASTER/Source', 'GILLEQ/Source', 'dependencies/JUCE/modules', 'dependencies/JUCE/extras/Build/CMake']:
        for f in sorted((source / group).rglob('*')):
            if f.is_file() and f.suffix in ('.h', '.hpp', '.cpp', '.mm', '.c', '.cmake', '.txt', '.in', '.m'):
                selected[f.relative_to(source).as_posix()] = sha(f)
    for name in ['dry_48k.wav', 'provenance.json', 'ORIGIN-AND-LICENSE.md']:
        f = source / 'GILLDEREVERB/Tests/fixtures' / name
        selected[f.relative_to(source).as_posix()] = sha(f)
    return selected

def recipe_binding():
    return {name: sha(HERE / name) for name in read(HERE / 'staging-manifest.json')['files']}

def validate_baseline():
    expected = read(HERE / 'baseline-manifest.json')
    require(tree(HERE / 'baseline') == expected, 'Frozen pre-fix baseline bytes changed')

def cpp_source(p, override=None):
    def include(match):
        f = (p.parent / match[1]).resolve()
        if override and match[1].endswith('/Spectral.h'):
            f = override
        return '#include "' + f.as_posix() + '"'
    return re.sub(r'#include "([^"]+)"', include, p.read_text(encoding='utf-8'))

def generate(source, destination):
    g = destination / 'generated'
    g.mkdir()
    tests = source / 'GILLSTUDIO/Next03/Tests/Spectral'
    current = source / 'GILLSTUDIO/Next03/Source/Spectral/Spectral.h'
    old = HERE / 'baseline/GILLSTUDIO/Next03/Source/Spectral/Spectral.h'
    for suffix, header in [('current', current), ('baseline', old)]:
        (g / f'spectral-{suffix}.cpp').write_text(cpp_source(tests / 'spectral_tests.cpp', header), encoding='utf-8')
        (g / f'mono-{suffix}.cpp').write_text('#define SPECTRAL_HEADER "' + header.as_posix() + '"\n' + (HERE / 'SpectralMonoTests.cpp').read_text(), encoding='utf-8')
    for label in ['level', 'advanced']:
        (g / f'{label}.cpp').write_text(cpp_source(tests / f'{label}_tests.cpp'), encoding='utf-8')
    data = (source / 'GILLDEREVERB/Tests/fixtures/dry_48k.wav').read_bytes()
    require(data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'Invalid speech WAV')
    chunks = {}; offset = 12
    while offset + 8 <= len(data):
        size = struct.unpack_from('<I', data, offset+4)[0]
        chunks[data[offset:offset+4]] = data[offset+8:offset+8+size]
        offset += 8 + size + size % 2
    fmt = struct.unpack_from('<HHIIHH', chunks[b'fmt '])
    require(fmt[0] == 3 and fmt[1] == 1 and fmt[2] == 48000 and fmt[5] == 32, 'Speech fixture must be mono IEEE float32 at 48 kHz')
    require(len(chunks[b'data']) >= 7*48000*4, 'Speech fixture too short')
    voice = g / 'voice.f32'; voice.write_bytes(chunks[b'data'])
    noise = (HERE / 'NoiseControls.cpp').read_text().replace('#include "../../GILLSTUDIO/Next03/Tests/Spectral/spectral_tests.cpp"', cpp_source(tests / 'spectral_tests.cpp'))
    (g / 'noise-controls.cpp').write_text('#define VOICE_FILE "' + voice.as_posix() + '"\n' + noise, encoding='utf-8')
    level = cpp_source(tests / 'level_tests.cpp')
    modern = level.replace((source / 'GILLSTUDIO/Source/Spectral/Spectral.h').as_posix(), old.as_posix()).replace('p.denoise=q.denoise=0;', 'p.denoise=q.denoise=mode==0?.7:0;')
    require(modern != level, 'Frozen comparison injection failed')
    (g / 'frozen-regression.cpp').write_text(modern[:modern.rindex('int main()')] + 'int main(){try{compatibility();std::cout<<"FROZEN_UNCHANGED_PATHS_REGRESSION_PASS\\n";}catch(const std::exception&e){std::cerr<<e.what();return 1;}}', encoding='utf-8')
    return g

class Runner:
    def __init__(self, destination):
        self.destination = destination
        self.commands = []

    def run(self, label, args, expected=0, token=None, timeout=1800):
        args = list(map(str, args)); start = time.monotonic()
        result = subprocess.run(args, cwd=self.destination, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace', timeout=timeout)
        log = self.destination / (label + '.log'); log.write_text(result.stdout, encoding='utf-8')
        executable = shutil.which(args[0]) or args[0]
        self.commands.append(dict(label=label, argv=args, executable_sha256=sha(executable), cwd=str(self.destination), exit=result.returncode, expected_exit=expected, token=token, log=log.name, log_sha256=sha(log), seconds=round(time.monotonic()-start, 3)))
        require(result.returncode == expected and (not token or token in result.stdout), f'{label}: expected exit {expected}, got {result.returncode}; see {log}')
        print(label + ' passed', flush=True)
        return result.stdout

def host_config(name):
    config = read(HERE / 'configs' / (name + '.json'))
    # Preserve the existing eight rate/quality/main groups, then add one-sided
    # stereo keys for mono mains. RenderCases implements these as physical buses.
    for c in list(config):
        if c['main'] == 1 and c['mode'] == 'stereo':
            for side in ['left', 'right']:
                extra = copy.deepcopy(c); extra['id'] = c['id'].removesuffix('stereo') + side
                extra['mode'] = side; extra['keySide'] = side; config.append(extra)
    return config

def samples(p):
    a = array.array('f'); a.frombytes(Path(p).read_bytes())
    if sys.byteorder != 'little': a.byteswap()
    return a

def validate_host(name, rows, directory):
    require(len(rows) == len(host_config(name)), 'Host case count differs')
    require([r['config'] for r in rows] == host_config(name), 'Host configurations differ')
    for row in rows:
        require(row.get('layoutApplied') and 'error' not in row, 'Host bus layout/load failed')
        require(all('missing' not in setting for setting in row['configured']), 'Missing host parameter')
        require(row['nonfinite'] == 0 and row['auxMaxMutation'] == 0, 'Host nonfinite audio or mutated key')
        require(row['stateBytes'] > 0 and row['parameterRecallMaxError'] < 1e-5, 'Host state recall failed')
        if row['config']['mode'] == 'leak': require(row['peak'] < 1e-10, 'External key leaked to main')
    groups = [r for r in rows if r['config']['mode'] == 'silent']
    require(len(groups) == 8, 'Expected eight rate/quality/layout groups')
    for silent in groups:
        prefix = silent['config']['id'].removesuffix('silent')
        pick = lambda mode: next(r for r in rows if r['config']['id'] == prefix + mode)
        stereo, anti, disabled = pick('stereo'), pick('anti'), pick('disabled')
        require(stereo['rms'] < silent['rms']*.95, 'Positive external key action missing')
        require(abs(anti['rms']-stereo['rms']) < max(1e-5, stereo['rms']*.02), 'Anti-phase detector cancellation')
        if name == 'GILLSPECTRA PREVIEW':
            require(disabled['rms'] > 1e-8 and disabled['rms'] < silent['rms']*.95 and abs(disabled['rms']-pick('internal')['rms']) < 1e-8, 'Spectral absent-key main fallback')
        else:
            require(abs(disabled['rms']-silent['rms']) < 1e-8, 'Suite disabled/silent key mismatch')
        if silent['config']['main'] == 1:
            ref = samples(directory / (stereo['config']['id']+'.f32'))
            for mode in ['anti', 'left', 'right']:
                actual = samples(directory / (pick(mode)['config']['id']+'.f32'))
                require(len(ref) == len(actual) and max(abs(a-b) for a,b in zip(ref,actual)) < 2e-6, 'Mono strongest key waveform mismatch')

def safe_path(root, relative):
    p = (root / relative).resolve()
    require(p.is_relative_to(root.resolve()), 'Proof path escaped root')
    return p

def verify_result(native, result=None, bundle_manifests=None):
    native = Path(native).resolve(); report = read(native / 'native-build.json')
    gate = report['update22_regression']; proof = safe_path(native, gate['report']); result = result or read(proof)
    require(sha(proof) == gate['report_sha256'], 'Regression report digest mismatch')
    require(result['passed'] is True and result['architecture'] == report['architecture'] == gate['architecture'], 'Regression architecture/pass mismatch')
    require(result['source_sha256'] == report['source']['source_sha256'], 'Regression source differs from pipeline')
    require(sha(native / 'native-build-before-update22.json') == result['native_report_sha256'] == gate['native_report_before_sha256'], 'Original native report changed')
    original=read(native/'native-build-before-update22.json');without_gate=copy.deepcopy(report);without_gate.pop('update22_regression')
    require(original==without_gate,'Native report changed beyond regression attachment')
    require(result['domain'] == 'native-macos', 'Local self-test cannot satisfy Mac gate')
    require(result['recipe_sha256'] == digest(result['recipe_inputs']) and result['source_inputs_sha256'] == digest(result['source_inputs']), 'Input manifest digest mismatch')
    require(result['recipe_inputs'] == recipe_binding(), 'Proof was executed by a different regression recipe')
    for rel, expected in result['proof_files'].items():
        require(sha(safe_path(proof.parent, rel)) == expected, 'Proof changed: ' + rel)
    require(set(result['bundles'])=={p['name'] for p in report['plugins']} and len(result['bundles'])==74,'Incomplete native bundle binding')
    for name, expected in result['bundles'].items():
        transported=bundle_manifests[name+'.vst3'] if bundle_manifests is not None else None
        require(digest(bundle_tree(native / 'plugins' / (name + '.vst3'),transported)) == expected, 'Native bundle changed: ' + name)
        require(next(p for p in report['plugins'] if p['name']==name)['bundle_sha256']==expected,'Regression bundle not bound to original native product')
    labels = {c['label'] for c in result['commands']}
    require({c[0] for c in CASES} | {'suite-current', 'suite-baseline', 'internal-Current', 'internal-Baseline','balance-ui21'} <= labels, 'Missing native regression commands')
    for c in result['commands']:
        text = safe_path(proof.parent, c['log']).read_text(encoding='utf-8')
        require(c['exit'] == c['expected_exit'] and (not c['token'] or c['token'] in text), 'Execution failed')
        require(sha(safe_path(proof.parent, c['log'])) == c['log_sha256'], 'Command log changed')
        for label,expected,token in CASES:
            if c['label']==label:require(c['expected_exit']==expected and c['token']==token,'Regression oracle was weakened')
        executable_name=Path(c['argv'][0]).name
        retained='build/'+executable_name
        if retained in result['proof_files']:
            require(result['proof_files'][retained]==c['executable_sha256'],'Executed binary differs from retained binary')
    for name in PRODUCTS:
        require('host-'+name in labels, 'Missing actual VST3 host product')
        validate_host(name, read(proof.parent / name / 'result.json'), proof.parent / name)
    require(len(result['state_products']) == 74 and set(result['state_products']) == {p['name'] for p in report['plugins']}, 'All 74 state products required')
    for name in result['state_products']:
        require('bool-'+name in labels, 'Missing native state sweep: '+name)
        state=read(proof.parent / 'state' / (name+'.json'))
        validate_state(state, name)
    require(sha(proof.parent / 'internal-Current.bin') == sha(proof.parent / 'internal-Baseline.bin'), 'Internal reference changed')
    return True

def validate_state(state, name):
    require(state.get('passed') is True and state.get('finite') is True and state.get('layoutApplied') is True and state['failures']==0, 'Native two-step state failed: '+name)
    require(state['checks'] == 8*state['twoStepParameters'] and state['checks']>0, 'Incomplete endpoint/fractional perturbation sweep')
    for parameter in state['parameters']:
        require(len(parameter['checks'])==8, 'Missing endpoint perturbation')
        for check in parameter['checks']:
            require(check['pass'] and abs(check['actual']-check['expected'])<1e-6, 'State recall mismatch')
            if check['policy'] != 'requested-endpoint':
                xml=check['serializedState']
                require(xml['xmlObservable'] and check['expected']==0, 'Canonical state must be decoded and neutral')
    if name=='GILLSMARTDEESSER':
        require(state['invalidProfileControl']['rejectedWithoutPartialApply'], 'SMART invalid profile was partially applied')

def execute(args):
    source = Path(args.source).resolve(); dest = Path(args.destination).resolve()
    require(not dest.exists(), 'Destination must be new')
    native = Path(args.native).resolve() if args.native else None
    if not args.self_test:
        require(sys.platform == 'darwin', 'Native regression proof requires macOS')
        require(platform.machine() in ('arm64', 'x86_64'), 'Unsupported native architecture')
        translated = subprocess.run(['sysctl','-n','sysctl.proc_translated'], capture_output=True, text=True)
        require(translated.stdout.strip() != '1', 'Rosetta does not prove native execution')
        require(native is not None, '--native is required')
        original = read(native / 'native-build.json')
        require(original['passed'] and original['architecture'] == platform.machine(), 'Incomplete or wrong-architecture native build')
        require('update22_regression' not in original, 'Native gate already attached; use --verify')
        require(dest.is_relative_to(native), 'Native proof destination must be inside native directory')
        specification=importlib.util.spec_from_file_location('mac_pipeline',source/'packaging/macos/pipeline.py')
        pipeline=importlib.util.module_from_spec(specification);specification.loader.exec_module(pipeline)
        require(pipeline.source_check(source)['source_sha256']==original['source']['source_sha256'], 'Source changed between normal build and regression')
    validate_baseline(); before = binding(source); recipe = recipe_binding()
    dest.mkdir(parents=True); runner = Runner(dest); generated = generate(source, dest)
    build = Path(args.self_test_cache).resolve() if args.self_test_cache else dest / 'build'
    command = [args.cmake, '-S', HERE, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSOURCE_ROOT='+source.as_posix(), '-DGENERATED='+generated.as_posix()]
    if args.spectral_only: command += ['-DSPECTRAL_ONLY=ON']
    if sys.platform == 'darwin':
        command += ['-DCMAKE_OSX_ARCHITECTURES='+platform.machine(), '-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0', '-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_C_COMPILER=clang']
    if args.ninja: command += ['-DCMAKE_MAKE_PROGRAM='+str(Path(args.ninja).resolve())]
    if args.juce_export: command += ['-DGILL_JUCE_EXPORT='+str(Path(args.juce_export).resolve()), '-DGILL_PREBUILT_RUNTIME='+str(Path(args.runtime).resolve())]
    runner.run('configure', command)
    runner.run('build', [args.cmake, '--build', build, '--parallel', args.jobs], timeout=3600)
    toolchain=dest/'toolchain';toolchain.mkdir()
    for metadata in [build/'CMakeCache.txt',build/'compile_commands.json',*build.glob('CMakeFiles/*/CMakeCXXCompiler.cmake')]:
        shutil.copy2(metadata,toolchain/metadata.name)
    compiler_definition=next(toolchain.glob('CMakeCXXCompiler.cmake')).read_text(encoding='utf-8')
    compiler_match=re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)',compiler_definition)
    require(compiler_match is not None,'Compiler identity missing')
    compiler=dict(path=compiler_match[1],sha256=sha(compiler_match[1]))
    if not args.self_test:require('set(CMAKE_CXX_COMPILER_ID "AppleClang")' in compiler_definition,'Native runner must use AppleClang')
    suffix = '.exe' if sys.platform == 'win32' else ''
    binaries = [build / (name+suffix) for name, _, _ in CASES]
    if not args.spectral_only: binaries += [build / (name+suffix) for name in ['SuiteCurrent','SuiteBaseline','Update22Host']]
    if args.self_test_cache:
        # Local development cache only. Copy freshly rebuilt executables so final
        # proof is retained independently of later cache rebuilds.
        retained=dest/'build';retained.mkdir()
        for executable in binaries:shutil.copy2(executable,retained/executable.name)
        build=retained;binaries=[build/executable.name for executable in binaries]
    for exe in binaries:
        if sys.platform == 'darwin': require(runner.run('arch-'+exe.name, ['lipo','-archs',exe]).strip() == platform.machine(), 'Wrong executable architecture')
    for label, expected, token in CASES:
        runner.run(label, [build / (label+suffix)], expected, token)
    if not args.spectral_only:
        runner.run('suite-baseline', [build / ('SuiteBaseline'+suffix), '--baseline'], token='BASELINE_DEFECTS_REPRODUCED')
        runner.run('suite-current', [build / ('SuiteCurrent'+suffix)], token='SIDE22_SUITE_NATIVE_PASS')
        for mode in ['Baseline','Current']:
            runner.run('internal-'+mode, [build / ('Suite'+mode+suffix), '--reference', dest / ('internal-'+mode+'.bin')], token='INTERNAL_REFERENCE_PASS')
        require(sha(dest/'internal-Baseline.bin') == sha(dest/'internal-Current.bin'), 'Internal-path audio changed')
    bundles = {}; state_products=[]
    if native:
        require(len(original['plugins'])==74, 'Normal build must contain all 74 plugins')
        require(args.build_root, '--build-root is required for existing BALANCE native UI target')
        balance=Path(args.build_root).resolve()/'GILLEFFECTS/GillBalanceUi21Tests'
        require(balance.is_file(), 'Normal pipeline BALANCE UI21 executable missing')
        require(runner.run('arch-balance-ui21',['lipo','-archs',balance]).strip()==platform.machine(), 'BALANCE executable architecture mismatch')
        saved_balance=dest/'GillBalanceUi21Tests';shutil.copy2(balance,saved_balance)
        runner.run('balance-ui21',[saved_balance],token='BALANCE_NATIVE_UI21_PASS')
        (dest/'state').mkdir()
        for product in original['plugins']:
            name=product['name']; bundle=native/'plugins'/(name+'.vst3')
            bundles[name]=digest(bundle_tree(bundle)); require(bundles[name]==product['bundle_sha256'], 'Native state bundle differs from pipeline')
            runner.run('bool-'+name,[build/('Update22Host'+suffix),bundle,dest/'state'/(name+'.json'),'bool'],token='HOST_BOOL_STATE_PASS')
            validate_state(read(dest/'state'/(name+'.json')),name); state_products.append(name)
        for name in PRODUCTS:
            bundle = native / 'plugins' / (name+'.vst3'); bundles[name] = digest(bundle_tree(bundle))
            require(next(p for p in original['plugins'] if p['name'] == name)['bundle_sha256'] == bundles[name], 'Bundle differs from normal pipeline build')
            directory = dest / name; directory.mkdir(); write(directory/'config.json', host_config(name))
            runner.run('host-'+name, [build/('Update22Host'+suffix),bundle,directory/'result.json',directory/'config.json'], token='HOST_RENDER_COMPLETE')
            validate_host(name, read(directory/'result.json'), directory)
    require(binding(source) == before and recipe_binding() == recipe, 'Source/recipe changed during regression')
    result = dict(passed=True, domain='windows-self-test' if args.self_test else 'native-macos', architecture=platform.machine(), source_inputs=before, source_inputs_sha256=digest(before), recipe_inputs=recipe, recipe_sha256=digest(recipe), commands=runner.commands, compiler=compiler, bundles=bundles, state_products=state_products, physical_fl_studio_tested=False)
    if native:
        require(pipeline.source_check(source)['source_sha256']==original['source']['source_sha256'], 'Source changed before native proof attachment')
        result.update(source_sha256=original['source']['source_sha256'], native_report_sha256=sha(native/'native-build.json'))
    # Preserve executed binaries and inputs, avoiding gigabytes of intermediate objects.
    result['proof_files'] = {f.relative_to(dest).as_posix(): sha(f) for f in sorted(dest.rglob('*')) if f.is_file() and ('build' not in f.relative_to(dest).parts or f in binaries)}
    write(dest/'result.json',result)
    if native:
        require(all(digest(bundle_tree(native/'plugins'/(name+'.vst3'))) == expected for name,expected in bundles.items()), 'Native bundle changed during regression')
        shutil.copyfile(native/'native-build.json',native/'native-build-before-update22.json')
        original['update22_regression'] = dict(passed=True, architecture=platform.machine(), report=(dest/'result.json').relative_to(native).as_posix(), report_sha256=sha(dest/'result.json'), native_report_before_sha256=result['native_report_sha256'])
        write(native/'native-build.json',original); verify_result(native)
        # Intermediates are not evidence; retain the exact executed executables.
        for child in build.iterdir():
            require(child.resolve().is_relative_to(build.resolve()),'Build cleanup path escaped proof')
            if child in binaries:continue
            require(not any(rel==child.relative_to(dest).as_posix() or rel.startswith(child.relative_to(dest).as_posix()+'/') for rel in result['proof_files']),'Refuse deleting bound proof')
            if child.is_dir():shutil.rmtree(child)
            else:child.unlink()
        verify_result(native)
    print('MAC22_NATIVE_REGRESSION_PASS' if native else 'MAC22_LOCAL_REGRESSION_PASS')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', default='work'); p.add_argument('--native'); p.add_argument('--destination'); p.add_argument('--verify');p.add_argument('--build-root')
    p.add_argument('--jobs',default='3'); p.add_argument('--cmake',default='cmake'); p.add_argument('--ninja'); p.add_argument('--juce-export'); p.add_argument('--runtime')
    p.add_argument('--self-test',action='store_true'); p.add_argument('--spectral-only',action='store_true')
    p.add_argument('--self-test-cache',help='Optional existing CMake cache; local self-test only, never native proof')
    args=p.parse_args()
    if args.verify: verify_result(Path(args.verify)); print('MAC22_NATIVE_REGRESSION_VERIFIED'); return
    require(args.destination, '--destination is required')
    require(not args.spectral_only or args.self_test, 'Native Mac proof cannot omit Suite/host tests')
    require(not args.self_test or not args.native, 'Self-test cannot attach native proof')
    require(not args.self_test_cache or args.self_test, 'Native proof requires a fresh build')
    execute(args)

if __name__ == '__main__':
    try: main()
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError, KeyError) as e:
        print('STOP: '+str(e),file=sys.stderr); sys.exit(1)
