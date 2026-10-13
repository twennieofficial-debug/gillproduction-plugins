"""Offline publisher contract tests. Tiny synthetic fixtures are NOT native Mac evidence."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import publish22 as pub

COMMIT = 'a' * 40
SOURCE = 'b' * 64


class FakeGitHub:
    def __init__(self):
        self.tag = None
        self.release = None
        self.assets = []
        self.events = []
        self.fail_upload = None
        self.bad_digest = False
        self.unknown_create = False

    def api(self, path, method='GET', payload=None, missing=False):
        self.events.append((method, path))
        if '/git/ref/' in path:
            return {'object': {'type': 'commit', 'sha': self.tag}} if self.tag else None
        if '/assets?' in path:
            return copy.deepcopy(self.assets)
        if method == 'POST':
            self.release = dict(payload, id=123)
            if self.unknown_create:
                self.unknown_create = False
                raise RuntimeError('Synthetic lost create response')
            return copy.deepcopy(self.release)
        if method == 'PATCH':
            self.release.update(payload)
            self.tag = self.release['target_commitish']
            return copy.deepcopy(self.release)
        return copy.deepcopy(self.release)

    def upload(self, release_id, path):
        if path.name == self.fail_upload:
            raise RuntimeError('Synthetic upload failure')
        digest = '0' * 64 if self.bad_digest else pub.core.sha(path)
        self.assets.append({'name': path.name, 'digest': 'sha256:' + digest, 'state': 'uploaded', 'size': path.stat().st_size})
        self.events.append(('UPLOAD', path.name))


class PublisherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = []
        for name in ('a.dmg', 'b.pkg', 'source.zip'):
            path = self.root / name
            path.write_bytes(b'SYNTHETIC UNIT FIXTURE: ' + name.encode())
            self.assets.append(path)
        self.api = FakeGitHub()

    def publish(self):
        return pub.publish(self.api, self.assets, COMMIT, SOURCE)

    def test_fresh_draft_then_verified_publish(self):
        self.assertEqual(self.publish(), pub.URL)
        self.assertFalse(self.api.release['draft'])
        actions = [e[0] for e in self.api.events]
        self.assertLess(actions.index('POST'), actions.index('UPLOAD'))
        self.assertGreater(actions.index('PATCH'), max(i for i, a in enumerate(actions) if a == 'UPLOAD'))
        self.assertEqual(len(self.api.assets), 3)

    def test_published_retry_no_mutations(self):
        self.publish()
        self.api.events.clear()
        self.assertEqual(self.publish(), pub.URL)
        self.assertTrue(all(method == 'GET' for method, _ in self.api.events))

    def test_partial_draft_resumes_missing_only(self):
        self.api.fail_upload = 'b.pkg'
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertTrue(self.api.release['draft'])
        self.api.fail_upload = None
        self.api.events.clear()
        self.publish()
        self.assertEqual([path for method, path in self.api.events if method == 'UPLOAD'], ['b.pkg', 'source.zip'])

    def test_unknown_create_outcome_recovers_same_release(self):
        self.api.unknown_create = True
        with self.assertRaises(RuntimeError):
            self.publish()
        self.api.events.clear()
        self.publish()
        self.assertNotIn('POST', [m for m, _ in self.api.events])

    def test_wrong_existing_tag_refused_without_mutation(self):
        self.api.tag = 'c' * 40
        with self.assertRaisesRegex(RuntimeError, 'tag points'):
            self.publish()
        self.assertIsNone(self.api.release)

    def test_bad_uploaded_digest_never_publishes(self):
        self.api.bad_digest = True
        with self.assertRaisesRegex(RuntimeError, 'digest unavailable'):
            self.publish()
        self.assertTrue(self.api.release['draft'])
        self.assertNotIn('PATCH', [m for m, _ in self.api.events])

    def test_extra_remote_asset_refused(self):
        self.publish()
        self.api.assets.append(dict(self.api.assets[0], name='unexpected.dmg'))
        with self.assertRaisesRegex(RuntimeError, 'Extra/duplicate'):
            self.publish()

    def test_duplicate_remote_asset_refused(self):
        self.publish()
        self.api.assets.append(copy.deepcopy(self.api.assets[0]))
        with self.assertRaisesRegex(RuntimeError, 'Extra/duplicate'):
            self.publish()

    def test_published_missing_asset_refused(self):
        self.publish()
        self.api.assets.pop()
        with self.assertRaisesRegex(RuntimeError, 'incomplete'):
            self.publish()

    def test_release_provenance_refused(self):
        self.publish()
        self.api.release['target_commitish'] = 'main'
        with self.assertRaisesRegex(RuntimeError, 'provenance'):
            self.publish()

    def test_local_change_refused_on_retry(self):
        self.publish()
        self.assets[0].write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'Remote asset differs'):
            self.publish()

    def test_missing_remote_digest_refused(self):
        self.publish()
        del self.api.assets[0]['digest']
        with self.assertRaisesRegex(RuntimeError, 'digest unavailable'):
            self.publish()

    def test_starter_asset_refused(self):
        self.publish()
        self.api.assets[0]['state'] = 'starter'
        with self.assertRaises(RuntimeError):
            self.publish()

    def test_duplicate_local_names_refused(self):
        self.assets.append(self.assets[0])
        with self.assertRaisesRegex(RuntimeError, 'Duplicate local'):
            self.publish()

    def test_api_errors_do_not_echo_credentials(self):
        result = type('Result', (), {'returncode': 1, 'stderr': 'GH_TOKEN=private-value', 'stdout': ''})()
        with patch.object(pub.subprocess, 'run', return_value=result), self.assertRaises(RuntimeError) as caught:
            pub.GitHub().api('repos/example/releases')
        self.assertNotIn('private-value', str(caught.exception))

    def test_api_payload_structured_no_shell(self):
        result = type('Result', (), {'returncode': 0, 'stderr': '', 'stdout': '{}'})()
        with patch.object(pub.subprocess, 'run', return_value=result) as run:
            pub.GitHub().api('repos/example/releases', 'POST', {'body': 'one\ntwo'})
        self.assertEqual(json.loads(run.call_args.kwargs['input']), {'body': 'one\ntwo'})
        self.assertNotIn('shell', run.call_args.kwargs)

    def test_only_404_means_missing(self):
        result = type('Result', (), {'returncode': 1, 'stderr': 'gh: not found (HTTP 404)', 'stdout': ''})()
        with patch.object(pub.subprocess, 'run', return_value=result):
            self.assertIsNone(pub.GitHub().api('repos/example/releases', missing=True))
            result.stderr = 'gh: forbidden (HTTP 403)'
            with self.assertRaises(RuntimeError):
                pub.GitHub().api('repos/example/releases', missing=True)

    def local_args(self):
        return SimpleNamespace(source=str(self.root / 'work'), delivery=str(self.root),
            universal=str(self.root / 'universal'), validations=str(self.root / 'validations'),
            source_archive=str(self.root / pub.ARCHIVE))

    def test_other_repository_stops_before_git(self):
        with patch.object(pub.core, 'native_arch'), patch.dict(pub.os.environ, {'GITHUB_REPOSITORY': 'other/repository', 'GITHUB_SHA': COMMIT}), patch.object(pub.core, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'repository or commit'):
                pub.verify_local(self.local_args())
            run.assert_not_called()

    def test_wrong_checkout_stops_before_native_validation(self):
        with patch.object(pub.core, 'native_arch'), patch.dict(pub.os.environ, {'GITHUB_REPOSITORY': pub.REPO, 'GITHUB_SHA': COMMIT}), patch.object(pub.core, 'run', return_value='d' * 40), patch.object(pub.individuals, 'verify_inputs') as verify:
            with self.assertRaisesRegex(RuntimeError, 'Checkout differs'):
                pub.verify_local(self.local_args())
            verify.assert_not_called()

    def test_native_gate_failure_stops_before_archive(self):
        with patch.object(pub.core, 'native_arch'), patch.dict(pub.os.environ, {'GITHUB_REPOSITORY': pub.REPO, 'GITHUB_SHA': COMMIT}), patch.object(pub.core, 'run', return_value=COMMIT), patch.object(pub.individuals, 'verify_inputs', side_effect=RuntimeError('native failed')), patch.object(pub.core, 'verify_source_archive') as archive:
            with self.assertRaisesRegex(RuntimeError, 'native failed'):
                pub.verify_local(self.local_args())
            archive.assert_not_called()

    def test_native_checkout_source_mismatch_stops(self):
        with patch.object(pub.core, 'native_arch'), patch.dict(pub.os.environ, {'GITHUB_REPOSITORY': pub.REPO, 'GITHUB_SHA': COMMIT}), patch.object(pub.core, 'run', return_value=COMMIT), patch.object(pub.individuals, 'verify_inputs', return_value={'source_sha256': SOURCE}), patch.object(pub.core, 'source_check', return_value={'source_sha256': 'f' * 64}):
            with self.assertRaisesRegex(RuntimeError, 'Native source differs'):
                pub.verify_local(self.local_args())


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / pub.ARCHIVE
        self.archive.write_bytes(b'SYNTHETIC, NOT A SOURCE ZIP')
        names = [p['name'] for p in pub.core.PRODUCTS]
        records = [{'name': n, 'bundle_sha256': hashlib.sha256(n.encode()).hexdigest()} for n in names]
        self.universal = {'source_sha256': SOURCE, 'signing': 'ad-hoc-local-test-only', 'plugins': records}
        self.manifest = {'release': '22', 'product_count': 74, 'gill_bundle_count': 73,
            'individual_dmg_count': 74, 'dmg_count': 75, 'architectures': ['arm64', 'x86_64'],
            'notarized': False, 'end_user_release_ready': False, 'fl_studio_tested': False,
            'signing': 'ad-hoc-local-test-only', 'installer_and_dmg_signing': 'unsigned',
            'native_validation_passed': True, 'installer_execution_passed': True,
            'source_sha256': SOURCE, 'source_archive_sha256': pub.core.sha(self.archive), 'deliveries': [], 'files': {}}
        for label in names + ['GILL-73-BUNDLE']:
            selected = [n for n in names if n != 'MOBINA'] if label == 'GILL-73-BUNDLE' else [label]
            base = pub.individuals.asset_base(label)
            pkg, dmg = self.root / (base + '.pkg'), self.root / (base + '.dmg')
            pkg.write_bytes(b'xar!SYNTHETIC, NOT AN INSTALLER')
            dmg.write_bytes(b'SYNTHETIC, NOT A DISK IMAGE' + b'koly' + b'\0' * 508)
            files = {p.name: pub.core.sha(p) for p in (pkg, dmg)}
            self.manifest['files'].update(files)
            self.manifest['deliveries'].append({'label': label, 'products': selected,
                'installer_execution_passed': True, 'mounted_dmg_passed': True, 'files': files})
            installed = {'passed': True, 'unrelated_plugins_unchanged': True, 'installer_sha256': files[pkg.name],
                'plugins': [p for p in records if p['name'] in selected]}
            mounted = {'passed': True, 'readonly_mount': True, 'verified_packaged_pkg_bytes': True,
                'dmg_sha256': files[dmg.name], 'files': {pkg.name: {'sha256': files[pkg.name]}}}
            if label == 'GILL-73-BUNDLE':
                mounted['files'][pub.ARCHIVE] = {'sha256': pub.core.sha(self.archive)}
            directory = self.root / 'verification' / label
            directory.mkdir(parents=True)
            for name, record in [('installer-test.json', installed), ('mounted-dmg-test.json', mounted)]:
                pub.core.write(directory / name, record)

    def validate(self):
        return pub.validate_delivery(self.root, self.manifest, self.universal, self.archive)

    def test_synthetic_structural_positive_control(self):
        assets = self.validate()
        self.assertEqual(len(assets), 77)
        self.assertEqual(sum(p.suffix == '.dmg' for p in assets), 75)
        self.assertEqual({p.name.split('-22-MAC')[0] for p in assets if p.suffix == '.pkg'}, {'MOBINA', 'GILL-73-BUNDLE'})

    def test_changed_dmg_refused(self):
        next(self.root.glob('*.dmg')).write_bytes(b'corrupt')
        with self.assertRaisesRegex(RuntimeError, 'Changed delivery'):
            self.validate()

    def test_missing_dmg_refused(self):
        next(self.root.glob('*.dmg')).unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing or linked'):
            self.validate()

    def test_fake_dmg_with_updated_hash_refused(self):
        path = next(self.root.glob('*.dmg'))
        path.write_bytes(b'not udif' * 100)
        digest = pub.core.sha(path)
        self.manifest['files'][path.name] = digest
        for item in self.manifest['deliveries']:
            if path.name in item['files']:
                item['files'][path.name] = digest
        with self.assertRaisesRegex(RuntimeError, 'UDIF'):
            self.validate()

    def test_mobina_in_main_bundle_refused(self):
        self.manifest['deliveries'][-1]['products'].append('MOBINA')
        with self.assertRaisesRegex(RuntimeError, 'membership'):
            self.validate()

    def test_wrong_source_refused(self):
        self.universal['source_sha256'] = 'c' * 64
        with self.assertRaisesRegex(RuntimeError, 'source/signing'):
            self.validate()

    def test_changed_archive_refused(self):
        self.archive.write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'Source ZIP changed'):
            self.validate()

    def test_false_native_claim_refused(self):
        self.manifest['native_validation_passed'] = False
        with self.assertRaisesRegex(RuntimeError, 'native_validation'):
            self.validate()

    def test_missing_install_evidence_refused(self):
        next((self.root / 'verification').glob('*/installer-test.json')).unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing or linked'):
            self.validate()

    def test_install_evidence_other_binary_refused(self):
        file = next((self.root / 'verification').glob('*/installer-test.json'))
        value = pub.core.read(file)
        value['plugins'][0]['bundle_sha256'] = 'e' * 64
        pub.core.write(file, value)
        with self.assertRaisesRegex(RuntimeError, 'binary evidence'):
            self.validate()

    def test_mounted_pkg_hash_mismatch_refused(self):
        file = next((self.root / 'verification').glob('*/mounted-dmg-test.json'))
        value = pub.core.read(file)
        next(iter(value['files'].values()))['sha256'] = 'e' * 64
        pub.core.write(file, value)
        with self.assertRaisesRegex(RuntimeError, 'Mounted PKG'):
            self.validate()

    def test_duplicate_delivery_refused(self):
        self.manifest['deliveries'][-1] = self.manifest['deliveries'][0]
        with self.assertRaisesRegex(RuntimeError, 'Missing/duplicate'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
