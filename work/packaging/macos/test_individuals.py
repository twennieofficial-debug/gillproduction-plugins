"""Recipe tests only: native installer evidence is emitted on macOS by individuals."""
import json
import os
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch

import individuals as delivery


def catalog():
    return [{'name': 'MOBINA' if i == 73 else f'GILL{i}', 'target': f'Plugin{i}',
             'bundle_id': f'com.gillproduction.p{i}', 'version': '0.20.0', 'group': 'Group'} for i in range(74)]


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.products = catalog()

    def test_split_and_stable_paths(self):
        self.assertEqual(len(delivery.validate_catalog(self.products)), 73)
        self.assertEqual(delivery.install_relative(self.products[-1]).as_posix(), 'Library/Audio/Plug-Ins/VST3/MOBINA/MOBINA.vst3')
        self.assertIn('/GILLPRODUCTION/', delivery.install_relative(self.products[0]).as_posix())

    def test_missing_product_rejected(self):
        with self.assertRaisesRegex(RuntimeError, '74'):
            delivery.validate_catalog(self.products[:-1])

    def test_duplicate_product_rejected(self):
        self.products[0]['name'] = self.products[1]['name'].lower()
        with self.assertRaisesRegex(RuntimeError, 'Duplicate'):
            delivery.validate_catalog(self.products)

    def test_unsafe_names_rejected(self):
        for name in ('../evil', '/tmp/evil', 'a\\b', 'x:y', '..', 'x\n', '-bad'):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                delivery.safe_name(name)

    def test_unsafe_source_group_rejected(self):
        self.products[0]['group'] = '../private'
        with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
            delivery.validate_catalog(self.products)

    def prepare_validation(self):
        plugins = self.root / 'plugins'
        plugins.mkdir()
        records = []
        for p in self.products:
            (plugins / (p['name'] + '.vst3')).mkdir()
            records.append({'name': p['name'], 'bundle_sha256': 'correct', 'mach_o': [{'architectures': ['arm64', 'x86_64']}]})
        report = {'plugins': records}
        for arch in ('arm64', 'x86_64'):
            folder = self.root / arch
            folder.mkdir()
            (folder / f'validation-{arch}.json').write_text(json.dumps(report))
        return report

    def validate_with_record(self, altered=None, mutate_report=None):
        report = self.prepare_validation()
        if mutate_report:
            mutate_report(report)
        def inspect(bundle, product, archs):
            result = {'name': product['name'], 'bundle_sha256': 'correct', 'mach_o': [{'architectures': ['arm64', 'x86_64']}]}
            if altered:
                altered(result)
            return result
        with patch.object(delivery.core, 'verify_release_gate', return_value=report), patch.object(delivery.core, 'inspect_bundle', side_effect=inspect), patch.object(delivery.core, 'run'):
            return delivery.verify_inputs(self.root, self.root, self.products)

    def test_valid_report_control(self):
        self.assertEqual(len(self.validate_with_record()['plugins']), 74)

    def test_bad_hash_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'manifest/hash'):
            self.validate_with_record(lambda r: r.update(bundle_sha256='tampered'))

    def test_wrong_architecture_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'architecture'):
            self.validate_with_record(lambda r: r.update(mach_o=[{'architectures': ['arm64']}]))

    def test_wrong_report_products_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'report product'):
            self.validate_with_record(mutate_report=lambda r: r['plugins'].__setitem__(0, r['plugins'][1]))

    def test_missing_actual_bundle_rejected(self):
        self.prepare_validation()
        (self.root / 'plugins/GILL0.vst3').rmdir()
        with self.assertRaisesRegex(RuntimeError, 'Missing or extra'):
            delivery.verify_inputs(self.root, self.root, self.products)

    def test_wrong_validation_product_list_rejected(self):
        report = self.prepare_validation()
        (self.root / 'arm64/validation-arm64.json').write_text(json.dumps({'plugins': report['plugins'][1:]}))
        with patch.object(delivery.core, 'verify_release_gate', return_value=report), self.assertRaisesRegex(RuntimeError, 'Validation product'):
            delivery.verify_inputs(self.root, self.root, self.products)

    def test_documents_disclose_unsigned_source_and_only_selected(self):
        archive = self.root / 'source.zip'
        archive.write_bytes(b'source')
        docs = self.root / 'docs'
        delivery.write_documents(docs, [self.products[-1]], archive, bundle=False)
        readme = (docs / 'ZUERST-LESEN.txt').read_text(encoding='utf-8')
        self.assertIn('nicht mit Apple Developer ID', readme)
        self.assertNotIn('/GILLPRODUCTION/', readme)
        self.assertIn(delivery.core.sha(archive), (docs / 'QUELLCODE.txt').read_text(encoding='utf-8'))
        self.assertEqual(json.loads((docs / 'products.json').read_text()), [self.products[-1]])

    def test_dmg_checks_bytes_and_detaches(self):
        disk, mount = self.root / 'disk', self.root / 'mount'
        disk.mkdir(); mount.mkdir()
        (disk / 'plugin.pkg').write_bytes(b'pkg')
        (mount / 'plugin.pkg').write_bytes(b'pkg')
        dmg = self.root / 'image.dmg'
        dmg.write_bytes(b'dmg')
        with patch.object(delivery.core, 'run') as run:
            self.assertTrue(delivery.test_dmg(dmg, disk, mount, self.root)['passed'])
            self.assertEqual(run.call_args_list[-1].args[0], ['hdiutil', 'detach', mount])
        (mount / 'plugin.pkg').write_bytes(b'tampered')
        with patch.object(delivery.core, 'run') as run, self.assertRaisesRegex(RuntimeError, 'Mounted DMG'):
            delivery.test_dmg(dmg, disk, mount, self.root)
        self.assertEqual(run.call_args_list[-1].args[0], ['hdiutil', 'detach', mount])

    def test_pkg_bundle_scope_and_metadata(self):
        work = self.root / 'work'
        work.mkdir()
        pkg = self.root / 'test.pkg'
        selected = self.products[:1]
        def run(command, **kwargs):
            if command[:2] == ['pkgbuild', '--analyze']:
                Path(command[-1]).write_bytes(plistlib.dumps([{'RootRelativeBundlePath': delivery.install_relative(selected[0]).as_posix()}]))
            if command[:2] == ['pkgutil', '--expand']:
                target = Path(command[-1]) / 'payload.pkg'
                target.mkdir(parents=True)
                (target / 'PackageInfo').write_text('<pkg-info identifier="com.test" version="0.22.0" install-location="/"/>')
        with patch.object(delivery.core, 'run', side_effect=run):
            delivery.make_pkg(self.root / 'payload', selected, work, pkg, 'com.test')
        definitions = plistlib.loads((work / 'components.plist').read_bytes())
        self.assertFalse(definitions[0]['BundleIsRelocatable'])
        self.assertFalse(definitions[0]['BundleIsVersionChecked'])
        self.assertEqual(definitions[0]['BundleOverwriteAction'], 'upgrade')
        self.assertIn('arm64,x86_64', (work / 'Distribution.xml').read_text())

    def test_pkg_wrong_bundle_rejected(self):
        def run(command, **kwargs):
            Path(command[-1]).write_bytes(plistlib.dumps([{'RootRelativeBundlePath': 'Library/evil.vst3'}]))
        with patch.object(delivery.core, 'run', side_effect=run), self.assertRaisesRegex(RuntimeError, 'wrong bundles'):
            delivery.make_pkg(self.root, self.products[:1], self.root, self.root / 'test.pkg', 'com.test')

    def test_non_disposable_installer_rejected(self):
        with patch.dict(os.environ, {}, clear=True), self.assertRaises(RuntimeError):
            delivery.core.require_disposable_install_runner(self.root / 'test.pkg')

    def exercise_install(self, *, tamper_unrelated=False, tamper_plugin=False):
        vst = self.root / 'VST3'
        vst.mkdir()
        other = vst / 'unrelated.txt'
        other.write_bytes(b'unchanged')
        selected = self.products[0]
        destination = vst / 'GILLPRODUCTION' / (selected['name'] + '.vst3')
        universal = self.root / 'universal'
        original = universal / 'plugins' / destination.name
        original.mkdir(parents=True)
        (original / 'binary').write_bytes(b'validated')
        payload = self.root / 'payload'
        payload.mkdir()
        pkg = self.root / 'plugin.pkg'
        pkg.write_bytes(b'installer')
        def run(command, **kwargs):
            if '/usr/sbin/installer' in command:
                destination.mkdir(parents=True)
                (destination / 'binary').write_bytes(b'changed' if tamper_plugin else b'validated')
                if tamper_unrelated:
                    other.write_bytes(b'tampered')
        with patch.object(delivery, 'VST3', vst), patch.object(delivery, 'install_relative', return_value=destination), \
             patch.object(delivery.core, 'require_disposable_install_runner'), patch.object(delivery.core, 'run', side_effect=run) as calls, \
             patch.object(delivery.core, 'inspect_bundle', return_value={'name': selected['name']}):
            result = delivery.test_install(pkg, universal, [selected], payload, self.root, cleanup=False)
            self.assertIn(['sudo', '-n', '/usr/sbin/installer', '-pkg', pkg, '-target', '/'], [c.args[0] for c in calls.call_args_list])
            return result

    def test_installer_executes_and_verifies_selected_and_unrelated(self):
        result = self.exercise_install()
        self.assertTrue(result['passed'])
        self.assertTrue(result['unrelated_plugins_unchanged'])

    def test_installer_detects_unrelated_modification(self):
        with self.assertRaisesRegex(RuntimeError, 'unrelated VST3'):
            self.exercise_install(tamper_unrelated=True)

    def test_installer_detects_changed_plugin(self):
        with self.assertRaisesRegex(RuntimeError, 'bundle file set differs'):
            self.exercise_install(tamper_plugin=True)


if __name__ == '__main__':
    unittest.main()
