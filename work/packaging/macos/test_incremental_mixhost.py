"""Offline controls. Mocks model orchestration only, never native test results."""
import copy
import io
import json
import os
from pathlib import Path
import tempfile
import tarfile
import unittest
from unittest.mock import patch
import zipfile
import incremental_mixhost as m


class ReuseGuards(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=os.environ.get('GILL_TEST_TMP'))
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_names_and_negative_controls(self):
        self.assertEqual(m.safe_name('native/plugins/GILLMIX.vst3/Contents/MacOS/GILLMIX'), 'native/plugins/GILLMIX.vst3/Contents/MacOS/GILLMIX')
        for name in ('../escape', '/absolute', 'C:/escape', 'a\\b', 'a//b', 'a/./b', 'a/../b', ''):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                m.safe_name(name)

    def test_zip_traversal_duplicates_links_and_parent_collision(self):
        cases = [[('../x', b'a')], [('same', b'a'), ('SAME', b'b')], [('a', b'a'), ('a/b', b'b')]]
        for entries in cases:
            data = io.BytesIO()
            with zipfile.ZipFile(data, 'w') as z:
                for n, b in entries:
                    z.writestr(n, b)
            data.seek(0)
            with zipfile.ZipFile(data) as z, self.assertRaises(RuntimeError):
                m.zip_index(z)
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as z:
            item = zipfile.ZipInfo('link'); item.external_attr = 0o120777 << 16
            z.writestr(item, '../outside')
        data.seek(0)
        with zipfile.ZipFile(data) as z, self.assertRaises(RuntimeError):
            m.zip_index(z)

    def test_transport_bytes_size_digest(self):
        f = self.root / 'artifact.zip'; f.write_bytes(b'actual bytes')
        meta = {'size_in_bytes': f.stat().st_size, 'digest': 'sha256:' + m.p.sha(f)}
        m.authenticate(f, meta)
        for change in ({'size_in_bytes': 1}, {'digest': 'sha256:' + '0'*64}):
            with self.assertRaises(RuntimeError):
                m.authenticate(f, dict(meta, **change))

    def test_unexpected_native_plugin_inventory_rejected(self):
        folder = self.root / 'native/plugins'; folder.mkdir(parents=True)
        (folder / 'Unexpected.vst3').mkdir()
        with self.assertRaisesRegex(RuntimeError, 'Unexpected original native plugin entry'):
            m.verify_payload(self.root, {}, 'arm64')

    def test_portable_native_tar_binds_bytes_and_original_mode(self):
        root = self.root / 'extracted'; root.mkdir()
        (root / 'host').write_bytes(b'original executable')
        archive = self.root / 'native.tar.gz'
        with tarfile.open(archive, 'w:gz') as t:
            entry = tarfile.TarInfo('./host'); entry.size = len(b'original executable'); entry.mode = 0o755
            t.addfile(entry, io.BytesIO(b'original executable'))
        result = m.transported_native_manifest(archive, root)
        self.assertEqual(result['host']['mode'], 0o755)
        self.assertEqual(result['host']['sha256'], m.p.sha(root / 'host'))
        (root / 'host').write_bytes(b'changed executable')
        with self.assertRaises(RuntimeError): m.transported_native_manifest(archive, root)

    def test_portable_thin_architecture_checks_actual_bytes(self):
        f = self.root / 'host'
        f.write_bytes(b'\xcf\xfa\xed\xfe' + (0x100000c).to_bytes(4, 'little'))
        m.require_thin_arch(f, 'arm64')
        with self.assertRaises(RuntimeError): m.require_thin_arch(f, 'x86_64')
        f.write_bytes(b'not code')
        with self.assertRaises(RuntimeError): m.require_thin_arch(f, 'arm64')

    def test_tar_rejects_traversal_links_duplicate_modes_and_oversize(self):
        cases = [('native/plugins', tarfile.DIRTYPE, 0o755, 0), ('../escape', tarfile.REGTYPE, 0o644, 0),
                 ('native/plugins/link', tarfile.SYMTYPE, 0o777, 0),
                 ('native/plugins/hard', tarfile.LNKTYPE, 0o644, 0),
                 ('native/plugins/device', tarfile.CHRTYPE, 0o644, 0),
                 ('native/plugins/setuid', tarfile.REGTYPE, 0o4755, 0)]
        for i, (name, kind, mode, size) in enumerate(cases[1:]):
            tarbytes = io.BytesIO()
            with tarfile.open(fileobj=tarbytes, mode='w:gz') as t:
                item = tarfile.TarInfo(name); item.type = kind; item.mode = mode; item.size = size; item.linkname = '../escape'
                t.addfile(item, io.BytesIO())
            archive = self.root / f'bad-{i}.zip'
            with zipfile.ZipFile(archive, 'w') as z: z.writestr('failed-native-binaries-arm64.tar.gz', tarbytes.getvalue())
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                m.unpack_binary_zip(archive, self.root / str(i), 'arm64')
        self.assertFalse((self.root.parent / 'escape').exists())

    def metadata(self):
        run = {'id': m.BASE_RUN, 'head_sha': m.BASE_COMMIT, 'run_attempt': 1,
               'repository': {'full_name': m.REPOSITORY}, 'head_repository': {'full_name': m.REPOSITORY},
               'event': 'workflow_dispatch', 'path': '.github/workflows/build-macos.yml'}
        jobs = [{'id': i, 'name': n, 'run_id': m.BASE_RUN, 'head_sha': m.BASE_COMMIT,
                 'status': 'completed', 'conclusion': 'failure'} for i, n in m.JOBS.values()]
        artifacts = [{'id': i, 'name': n, 'expired': False, 'size_in_bytes': 5, 'digest': 'sha256:' + 'a'*64,
                      'workflow_run': {'id': m.BASE_RUN, 'head_sha': m.BASE_COMMIT}}
                     for i, n in [(11656663433, 'failed-native-arm64'), (11656753556, 'failed-native-binaries-arm64'), (123, 'failed-native-x86_64')]]
        return {'run': run, 'jobs': jobs, 'artifacts': artifacts}

    def test_remote_provenance_rejects_wrong_identity_pending_and_missing(self):
        baseline = self.metadata()
        self.assertEqual(len(m.validate_remote_metadata(baseline, 'arm64')), 3)
        changes = [lambda x: x['run'].update(id=7), lambda x: x['run'].update(head_sha='a'*40),
                   lambda x: x['run'].update(run_attempt=2), lambda x: x['run'].update(event='pull_request'),
                   lambda x: x['run']['repository'].update(full_name='other/repo'),
                   lambda x: x['jobs'][0].update(status='in_progress'), lambda x: x['jobs'][0].update(conclusion='success'),
                   lambda x: x['jobs'][0].update(id=1), lambda x: x['artifacts'].pop(),
                   lambda x: x['artifacts'][0].update(expired=True), lambda x: x['artifacts'][0].update(id=1),
                   lambda x: x['artifacts'][0].update(digest=''), lambda x: x['artifacts'][0]['workflow_run'].update(head_sha='b'*40)]
        for change in changes:
            value = copy.deepcopy(baseline); change(value)
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                m.validate_remote_metadata(value, 'arm64')

    def test_no_native_hosts_when_quality_fails(self):
        with patch.object(m.p, 'build_quality_host', side_effect=RuntimeError('native error')) as q, patch.object(m.p, 'build_mix_host') as mix:
            with self.assertRaisesRegex(RuntimeError, 'native error'):
                m.execute_hosts(self.root, self.root, self.root, 'arm64', 2, [], {'source_sha256': 'a'*64})
            q.assert_called_once(); mix.assert_not_called()

    def test_real_helpers_both_invoked_and_malformed_result_rejected(self):
        order = []
        def q(*args): order.append(('quality', args)); return {}
        def mix(*args): order.append(('mix', args)); return {}
        with patch.object(m.p, 'build_quality_host', side_effect=q), patch.object(m.p, 'build_mix_host', side_effect=mix):
            with self.assertRaises(RuntimeError):
                m.execute_hosts(self.root, self.root, self.root, 'arm64', 2, [], {'source_sha256': 'a'*64})
        self.assertEqual([n for n, _ in order], ['quality', 'mix'])
        self.assertEqual(order[0][1][3:5], ('arm64', 2))

    def test_build_rejects_source_before_any_host_or_output(self):
        from types import SimpleNamespace
        args = SimpleNamespace(source=str(self.root))
        with (patch.object(m.p, 'native_arch', return_value='arm64'), patch.object(m, 'fresh_provenance') as gh,
              patch.object(m.p, 'build_quality_host') as host):
            with self.assertRaisesRegex(RuntimeError, 'Imported build helpers'):
                m.build(args)
            host.assert_not_called(); gh.assert_not_called()


@unittest.skipUnless(os.environ.get('GILL_MIX_ACTUAL_FIXTURE'), 'Actual retained ARM fixture supplied by leaf verifier')
class ActualSourceAndEvidenceControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads(Path(os.environ['GILL_MIX_ACTUAL_FIXTURE']).read_text())
        cls.base = cls.fixture['base']; cls.current = cls.fixture['current']
        cls.source = Path(cls.fixture['source']); cls.original = Path(cls.fixture['original']).read_bytes()

    def guard(self, current=None, base=None, approved=None):
        with patch.object(m.p, 'HERE', Path(self.fixture['recipes'])):
            return m.verify_source_closure(base or self.base, current or self.current, self.source, self.original,
                                           approved or self.current['files'][m.HOST])

    def test_actual_source_positive_then_all_forbidden_changes(self):
        proof = self.guard()
        self.assertEqual(proof['changed_files'], [m.HOST])
        for name in self.current['files']:
            if name == m.HOST:
                continue
            value = copy.deepcopy(self.current)
            value['files'][name] = '0' * len(value['files'][name])
            value['source_sha256'] = m.p.digest_manifest(value['files'])
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                self.guard(value)

    def test_actual_added_missing_or_unapproved_host(self):
        for action in ('add', 'remove', 'host'):
            value = copy.deepcopy(self.current)
            if action == 'add': value['files']['GILLCommon/new.h'] = '0'*64
            elif action == 'remove': value['files'].pop('GILLCommon/LiveCausal.h')
            else: value['files'][m.HOST] = '0'*64
            value['source_sha256'] = m.p.digest_manifest(value['files'])
            with self.subTest(action=action), self.assertRaises(RuntimeError): self.guard(value)
        with self.assertRaises(RuntimeError): self.guard(approved='b'*64)

    def test_actual_missing_physical_source_rejected(self):
        target = self.source / m.HOST
        original = target.read_bytes()
        try:
            target.write_bytes(original + b'\n// extra\n')
            with self.assertRaises(RuntimeError): self.guard()
        finally:
            target.write_bytes(original)

    def test_actual_ui_transform_does_not_allow_assertion_deletion(self):
        actual = m.approved_host_bytes(self.original)
        self.assertEqual(m.byte_sha(actual), self.current['files'][m.HOST])
        with self.assertRaises(RuntimeError): m.approved_host_bytes(self.original + b' ')

    def test_actual_evidence_corruptions_fail(self):
        # Alter one inspected byte sequence while retaining the genuine rest;
        # ZIP transport authentication separately rejects changed archive bytes.
        original = m.zip_read
        mutations = [
            ('native-failure.json', lambda b: b.replace(b'"failed_groups": []', b'"failed_groups": [{}]')),
            ('logs/GILLMIX-ctest.txt', lambda b: b.replace(b'Passed', b'Failed', 1)),
            ('test-evidence/MIX_HOST/mix-host-48000.json', lambda b: b.replace(b'"failures": 1', b'"failures": 2')),
            ('logs/MixRealHost-48000.txt', lambda b: b + b'\nFAIL other DSP assertion\n'),
            ('logs/MixRealHost-48000.txt', lambda b: b.replace(b'accessibilityPerformPress APPLY: accepted', b'accessibilityPerformPress APPLY: rejected')),
            ('test-evidence/QUALITY_HOST/quality-host-44100.json', lambda b: b.replace(b'"failures": 0', b'"failures": 1', 1))]
        for target, transform in mutations:
            def changed(z, index, name):
                raw = original(z, index, name)
                if name == target:
                    new = transform(raw); self.assertNotEqual(raw, new); return new
                return raw
            with self.subTest(target=target), patch.object(m, 'zip_read', side_effect=changed), self.assertRaises(RuntimeError):
                m.validate_evidence_zip(self.fixture['diagnostic_zip'], 'arm64')

    def test_real_helper_commands_run_four_quality_rates_then_mix_and_reject_stale_reports(self):
        # Fake command boundary, real pipeline host orchestration and validators.
        # This is a unit test of scheduling, explicitly not native execution.
        with tempfile.TemporaryDirectory(dir=os.environ.get('GILL_TEST_TMP')) as directory:
            root = Path(directory); dest = root / 'dest'; build = root / 'build'; dest.mkdir()
            with zipfile.ZipFile(self.fixture['diagnostic_zip']) as z:
                quality = json.loads(z.read('test-evidence/QUALITY_HOST/quality-host-gate.json'))
                mixed = json.loads(z.read('test-evidence/MIX_HOST/mix-host-48000.json'))
            records = [{'name': n, 'bundle_sha256': h} for n, h in quality['bundle_sha256'].items()]
            events = []
            def command(args, **kwargs):
                args = list(map(str, args)); events.append(args)
                if args[0] == 'cmake' and args[1] == '--build':
                    folder = Path(args[2]); folder.mkdir(parents=True, exist_ok=True)
                    exe = 'GillQualityHost' if folder.name == 'QualityHost' else 'GillMixRealHost'
                    (folder / exe).write_bytes(b'unit-test-command-boundary')
                elif args[0] == 'lipo': return 'arm64'
                elif Path(args[0]).name == 'GillQualityHost':
                    rate = int(args[4]); result = copy.deepcopy(next(r['result'] for r in quality['runs'] if r['sample_rate'] == rate))
                    for item in result['products']: item['bundle'] = str(dest / 'plugins' / (item['name'] + '.vst3'))
                    Path(args[6]).write_text(json.dumps(result))
                elif Path(args[0]).name == 'GillMixRealHost':
                    result = copy.deepcopy(mixed); result.update(passed=True, failures=0)
                    for item in result['products']: item['bundle'] = str(dest / 'plugins' / (item['name'] + '.vst3'))
                    Path(args[4]).write_text(json.dumps(result))
                    log = Path(kwargs['log']); log.parent.mkdir(parents=True, exist_ok=True)
                    log.write_text('RESULT 184 checks, 0 failures\n')
                return ''
            with (patch.object(m.p, 'run', side_effect=command), patch.object(m.p, 'macho', return_value=True),
                  patch.object(m.p, 'bundle_digest', side_effect=lambda path: quality['bundle_sha256'][Path(path).name[:-5]])):
                m.execute_hosts(self.source, build, dest, 'arm64', 2, records, self.current)
                runs = [e for e in events if Path(e[0]).name in ('GillQualityHost', 'GillMixRealHost')]
                self.assertEqual([int(e[4]) for e in runs[:-1]], [44100, 48000, 96000, 192000])
                self.assertEqual(Path(runs[-1][0]).name, 'GillMixRealHost')
                self.assertEqual(len([e for e in events if e[:2] == ['cmake', '--build']]), 2)
                with patch.object(m.p, 'diagnose_quality_failure', return_value={}):
                    with self.assertRaises(RuntimeError):
                        m.execute_hosts(self.source, build, dest, 'arm64', 2, records, self.current)


if __name__ == '__main__':
    unittest.main()
