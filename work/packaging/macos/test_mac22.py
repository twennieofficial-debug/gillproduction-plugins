"""Mac22 controls, all synthetic. No native build or publication in this suite."""
import copy
import json
import re
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import individuals as ind
import pipeline as core
import publish22 as pub
from test_publish22 import FakeGitHub, COMMIT, SOURCE


class Mac22Controls(unittest.TestCase):
    def test_quality_host_version_table_matches_all_current_factory_versions(self):
        host = core.HERE.parents[1] / 'GILLCommon/Tests/QualityHost.cpp'
        pairs = re.findall(r'\{"([^"]+)",\s*"([0-9]+\.[0-9]+\.[0-9]+)"\}', host.read_text(encoding='utf-8'))
        expected = {p['name']: p['version'] for p in core.PRODUCTS}
        self.assertEqual(len(pairs), len(expected), 'Missing or duplicate native-host version entries')
        self.assertEqual(dict(pairs), expected, 'Native QualityHost expects obsolete product versions')
        # The exact failed Mac20 table must be rejected before a long native build.
        old = {name: '0.20.0' for name in expected}
        self.assertNotEqual(old, expected)
        self.assertEqual(sum(old[name] != version for name, version in expected.items()), 13)

    def test_full_catalog_source_fingerprint_binds_nested_spectral_and_regression_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            required = ['GILLCommon/' + n for n in ('QualityBus.h','QualityUi.h','MaterialUi.h','Tests/QualityHost.cpp','Tests/MacQualityHost.mm','Tests/QualityHostCMake/CMakeLists.txt')]
            required += [g+'/CMakeLists.txt' for g in core.GROUPS]
            required += ['GILLMIX/Tests/'+n for n in ('RealHost.cpp','MacRealHost.mm','HostCMake/CMakeLists.txt')]
            required += ['GILLSTUDIO/Next03/Tests/Spectral/'+n for n in ('spectral_tests.cpp','level_tests.cpp','advanced_tests.cpp')]
            required += ['GILLSTUDIO/Source/Dynamics/Dynamics.h', 'GILLSTUDIO/Source/Spectral/Spectral.h','GILLSTUDIO/Next03/Source/Spectral/Spectral.h',
                         'Effects16/advanced-ui/AdvancedScalpel.h','Effects16/creative/before/CreativeDSP.h',
                         'Effects16/creative/before-tempo/CreativeDSP.h','dependencies/JUCE/CMakeLists.txt',
                         'Mac22/regression/run.py','Mac22/regression/baseline/Engine.h']
            for name in required:
                path=root/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(b'synthetic input')
            before=core.source_check(root)
            for name in required:
                if name!='dependencies/JUCE/CMakeLists.txt': self.assertIn(name,before['files'])
            (root/'Mac22/regression/baseline/Engine.h').write_bytes(b'changed')
            self.assertNotEqual(before['source_sha256'],core.source_check(root)['source_sha256'])
            (root/'GILLSTUDIO/Next03/Tests/Spectral/level_tests.cpp').unlink()
            with self.assertRaisesRegex(RuntimeError,'Missing standalone spectral'):
                core.source_check(root)

    def test_space_product_names_are_canonical_before_package_and_upload(self):
        names = [ind.asset_base(p['name']) for p in core.PRODUCTS]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue(all(' ' not in n for n in names))
        for name in ('GILLSPECTRA PREVIEW', 'GILLTUNE LIVE'):
            self.assertIn(name.replace(' ', '.') + '-22-MAC-' + ind.STATUS, names)
        with tempfile.TemporaryDirectory() as temp:
            assets = []
            for name in names:
                path = Path(temp) / (name + '.dmg')
                path.write_bytes(b'synthetic')
                assets.append(path)
            api = FakeGitHub()
            pub.publish(api, assets, COMMIT, SOURCE)
            self.assertEqual({p.name for p in assets}, {a['name'] for a in api.assets})
            self.assertEqual(api.release['tag_name'], 'mac-bundle-22')
            for bad in ('Ã', 'â€', 'Â'):
                self.assertNotIn(bad, api.release['body'] + api.release['name'])
            self.assertIn('Hörprüfung', api.release['body'])

    def test_noncanonical_asset_rejected_before_remote_call(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'space name.dmg'; path.write_bytes(b'synthetic')
            api = FakeGitHub()
            with self.assertRaisesRegex(RuntimeError, 'Noncanonical'):
                pub.publish(api, [path], COMMIT, SOURCE)
            self.assertEqual(api.events, [])

    def test_upload_response_alias_or_hash_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'canonical.dmg'; path.write_bytes(b'synthetic')
            record = {'name': path.name, 'state': 'uploaded', 'size': path.stat().st_size,
                      'digest': 'sha256:' + core.sha(path)}
            for mutate in (lambda r: None, lambda r: r.update(name='alias.dmg'), lambda r: r.update(digest='sha256:'+'0'*64)):
                item = copy.deepcopy(record); mutate(item)
                result = type('Result', (), {'returncode': 0, 'stdout': json.dumps(item).encode()})()
                with patch.object(pub.subprocess, 'run', return_value=result):
                    if item == record:
                        pub.GitHub().upload(123, path)
                    else:
                        with self.assertRaises(RuntimeError): pub.GitHub().upload(123, path)

    def test_update22_required_and_architecture_bound(self):
        good = {'architecture': 'arm64', 'update22_regression': {
            'passed': True, 'architecture': 'arm64', 'report': 'test-evidence/UPDATE22/result.json',
            'report_sha256': 'a'*64, 'native_report_before_sha256': 'b'*64}}
        core.verify_update22_gate(good)
        for mutate in (lambda r: r.pop('update22_regression'),
                       lambda r: r['update22_regression'].update(architecture='x86_64'),
                       lambda r: r['update22_regression'].update(passed=False),
                       lambda r: r['update22_regression'].update(report='../../other.json')):
            bad = copy.deepcopy(good); mutate(bad)
            with self.assertRaises(RuntimeError): core.verify_update22_gate(bad)

    def test_current_catalog_versions_are_preserved(self):
        self.assertEqual({p['version'] for p in core.PRODUCTS}, {'0.20.0', '0.21.0'})
        ind.validate_catalog(core.PRODUCTS)
        bad = copy.deepcopy(core.PRODUCTS); bad[0]['version'] = '0.22.0'
        with self.assertRaisesRegex(RuntimeError, 'version mismatch'): ind.validate_catalog(bad)


if __name__ == '__main__': unittest.main()
