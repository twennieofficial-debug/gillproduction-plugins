import argparse
import importlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import plistlib
import struct
import zlib
from unittest.mock import Mock, patch
import recover as r
import diagnose

class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def adapter(self, responses):
        original = Mock(return_value='created')
        execute = Mock(side_effect=[subprocess.CompletedProcess([], code, '', text) for code, text in responses])
        return r.DmgAdapter(original, self.root / 'diagnostics', execute=execute, sleep=Mock())

    def test_explicit_hfs_preserves_payload_volume_and_format(self):
        adapter = self.adapter([])
        command = ['hdiutil', 'create', '-volname', 'GILLRACK', '-srcfolder', '/tmp/disk', '-format', 'UDZO', '/tmp/output.dmg']
        adapter(command)
        actual = adapter.original.call_args_list[0].args[0]
        self.assertEqual(actual, command[:-1] + ['-fs', 'HFS+'] + command[-1:])
        self.assertEqual(command[-1], '/tmp/output.dmg')
        self.assertEqual(len(adapter.creates), 1)

    def test_create_failure_propagates(self):
        adapter = self.adapter([])
        adapter.original.side_effect = RuntimeError('create failed')
        with self.assertRaisesRegex(RuntimeError, 'create failed'):
            adapter(['hdiutil', 'create', '-format', 'UDZO', 'a.dmg'])
        self.assertFalse(adapter.creates)

    def test_verify_success_is_real_exit_zero(self):
        adapter = self.adapter([(0, 'verified')])
        self.assertEqual(adapter(['hdiutil', 'verify', 'a.dmg'], log=self.root / 'verify.txt'), 'verified')
        self.assertTrue(adapter.verifications[0]['passed'])
        adapter.sleep.assert_not_called()

    def test_transient_then_actual_success(self):
        adapter = self.adapter([(1, 'hdiutil: verify failed - Resource temporarily unavailable\n'), (0, 'OK')])
        self.assertEqual(adapter(['hdiutil', 'verify', 'a.dmg']), 'OK')
        self.assertEqual(adapter.execute.call_count, 2)
        self.assertEqual([x['exit'] for x in adapter.verifications[0]['attempts']], [1, 0])

    def test_exhausted_eagain_stays_failure(self):
        adapter = self.adapter([(1, 'hdiutil: verify failed - Resource temporarily unavailable')]*3)
        with self.assertRaises(RuntimeError):
            adapter(['hdiutil', 'verify', 'a.dmg'])
        self.assertEqual(adapter.execute.call_count, 3)
        self.assertFalse(adapter.verifications)

    def test_unsupported_and_generic_errors_never_accepted_or_retried(self):
        for code, text in [(1, 'not recognized'), (1, 'Resource temporarily unavailable'),
                           (5, 'hdiutil: verify failed - Resource temporarily unavailable')]:
            with self.subTest(code=code, text=text):
                adapter = self.adapter([(code, text)])
                with self.assertRaises(RuntimeError):
                    adapter(['hdiutil', 'verify', 'a.dmg'])
                self.assertEqual(adapter.execute.call_count, 1)

    def test_verify_timeout_propagates(self):
        adapter = self.adapter([])
        adapter.execute.side_effect = subprocess.TimeoutExpired('hdiutil', 1)
        with self.assertRaises(subprocess.TimeoutExpired):
            adapter(['hdiutil', 'verify', 'a.dmg'])
        self.assertFalse(adapter.verifications)

    def test_other_commands_unchanged(self):
        adapter = self.adapter([])
        for cmd in [['hdiutil', 'attach', '-readonly', 'a.dmg'], ['sudo', '-n', '/usr/sbin/installer', '-pkg', 'a.pkg', '-target', '/']]:
            adapter(cmd)
            self.assertEqual(adapter.original.call_args.args[0], cmd)

    def test_wrong_upstream_identity_source_digest_rejected(self):
        name = 'universal'
        identity, digest = r.INPUTS[name]
        valid = dict(id=identity, name=name, digest='sha256:'+digest, expired=False,
                     size_in_bytes=r.INPUT_BYTES[name],
                     workflow_run=dict(id=r.RUN, head_sha=r.FROZEN))
        r.validate_metadata(name, valid)
        for key, value in [('id', 1), ('name', 'wrong'), ('digest', 'sha256:'+'0'*64), ('expired', True), ('size_in_bytes', 1),
                           ('workflow_run', dict(id=1, head_sha=r.FROZEN)),
                           ('workflow_run', dict(id=r.RUN, head_sha='wrong'))]:
            with self.subTest(key=key):
                with self.assertRaises(RuntimeError):
                    r.validate_metadata(name, {**valid, key:value})

    def test_zip_bytes_rejected_before_extraction(self):
        archive = self.root / 'wrong.zip'
        archive.write_bytes(b'wrong upstream zip')
        with self.assertRaisesRegex(RuntimeError, 'Wrong upstream ZIP'):
            r.restore('universal', archive, self.root)
        self.assertFalse((self.root / 'universal').exists())

    def test_archive_path_controls(self):
        r.safe_members(['a/b', './c'])
        for names in [['../x'], ['/x'], ['C:/x'], ['a\\x'], ['a', 'a']]:
            with self.assertRaises(RuntimeError):
                r.safe_members(names)

    def test_zip_hash_and_size_positive_then_tampered(self):
        archive = self.root / 'fixture.zip'; archive.write_bytes(b'fixture')
        with patch.dict(r.INPUTS, {'universal': (1, r.sha(archive))}), patch.dict(r.INPUT_BYTES, {'universal': 7}):
            r.verify_zip('universal', archive)
            archive.write_bytes(b'changed')
            with self.assertRaises(RuntimeError):
                r.verify_zip('universal', archive)

    def test_wrong_frozen_checkout_dirty_or_fingerprint_rejected(self):
        core = Mock()
        core.source_check.return_value = {'source_sha256': r.SOURCE}
        with patch.object(r, 'git', side_effect=[r.FROZEN, '']):
            self.assertEqual(r.checked_source(core, self.root / 'work')['source_sha256'], r.SOURCE)
        for values in [['wrong'], [r.FROZEN, 'M file']]:
            with patch.object(r, 'git', side_effect=values), self.assertRaises(RuntimeError):
                r.checked_source(core, self.root / 'work')
        core.source_check.return_value = {'source_sha256':'wrong'}
        with patch.object(r, 'git', side_effect=[r.FROZEN, '']), self.assertRaises(RuntimeError):
            r.checked_source(core, self.root / 'work')

    def test_offline_report_is_not_native_success(self):
        report = json.loads((Path(__file__).parent / 'udif-diagnosis.json').read_text())
        self.assertEqual(set(report), {'GILLRACK', 'GILLPRECISION'})
        for entry in report.values():
            self.assertFalse(entry['native_hdiutil_verified'])
            self.assertTrue(entry['all_blkx_crc32_match'])
            self.assertEqual(len(entry['blocks']), 8)
            self.assertTrue(any('Apple_APFS' in b['name'] for b in entry['blocks']))
        with self.assertRaises(AssertionError):
            diagnose.inspect(b'not a disk image')

    def test_udif_parser_positive_then_corrupt_crc_and_trailer(self):
        payload = b'A'*512
        block = bytearray(284); block[:4] = b'mish'
        struct.pack_into('>I', block, 64, 2)
        struct.pack_into('>I', block, 72, zlib.crc32(payload))
        struct.pack_into('>I', block, 200, 2)
        struct.pack_into('>IIQQQQ', block, 204, 1, 0, 0, 1, 0, 512)
        struct.pack_into('>IIQQQQ', block, 244, 0xffffffff, 0, 1, 0, 0, 0)
        xml = plistlib.dumps({'resource-fork':{'blkx':[{'Name':'fixture', 'Data':bytes(block)}]}})
        trailer = bytearray(512); trailer[:4] = b'koly'
        struct.pack_into('>II', trailer, 4, 4, 512)
        struct.pack_into('>QQ', trailer, 24, 0, 512)
        struct.pack_into('>QQ', trailer, 216, 512, len(xml))
        fixture = payload + xml + bytes(trailer)
        self.assertTrue(diagnose.inspect(fixture)['all_blkx_crc32_match'])
        for corrupt in [b'B'+fixture[1:], fixture[:-1]]:
            with self.assertRaises(AssertionError):
                diagnose.inspect(corrupt)

class FrozenRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        here = Path(__file__).resolve().parent
        candidates = [here.parent / 'repository/work/packaging/macos', here.parents[1] / 'frozen/work/packaging/macos']
        cls.frozen = next((p for p in candidates if (p / 'individuals.py').is_file()), None)
        if cls.frozen is None:
            raise RuntimeError('Frozen recipe checkout is required for integration tests')
        sys.path.insert(0, str(cls.frozen))
        cls.core = importlib.import_module('pipeline')
        cls.individuals = importlib.import_module('individuals')

    def test_frozen_recipe_bytes_are_original(self):
        self.assertEqual(r.sha(self.frozen / 'individuals.py'), '49e8bde94ee65ff8d1b5cd54b371c72bf0491b941bf1e0f2df00f5579c74915a')
        self.assertEqual(r.sha(self.frozen / 'pipeline.py'), 'ad249d832611464112dd769d7614da05799706dcb920f82421b6cd0094832135')

    def exercise_mount(self, failure=None):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); disk = root / 'disk'; disk.mkdir(); mount = root / 'mount'
            (disk / 'shipped.pkg').write_bytes(b'actual package bytes')
            dmg = root / 'shipped.dmg'; dmg.write_bytes(b'test fixture only')
            commands = []
            def run(cmd, **kwargs):
                commands.append([str(x) for x in cmd])
                if cmd[1] == 'verify' and failure == 'verify':
                    raise RuntimeError('verify failed')
                if cmd[1] == 'attach':
                    self.assertIn('-readonly', cmd)
                    if failure == 'attach':
                        raise RuntimeError('attach failed')
                    shutil.copy2(disk / 'shipped.pkg', mount / 'shipped.pkg')
                    if failure == 'bytes':
                        (mount / 'shipped.pkg').write_bytes(b'altered')
                return ''
            with patch.object(self.core, 'run', side_effect=run):
                if failure:
                    with self.assertRaises(RuntimeError):
                        self.individuals.test_dmg(dmg, disk, mount, root / 'evidence')
                    self.assertFalse((root / 'evidence/mounted-dmg-test.json').exists())
                else:
                    result = self.individuals.test_dmg(dmg, disk, mount, root / 'evidence')
                    self.assertTrue(result['passed'] and result['readonly_mount'] and result['verified_packaged_pkg_bytes'])
            return commands

    def test_original_readonly_mount_and_byte_check_positive(self):
        self.assertEqual([x[1] for x in self.exercise_mount()], ['verify', 'attach', 'detach'])

    def test_original_mount_checks_fail_closed(self):
        for failure in ['verify', 'attach', 'bytes']:
            with self.subTest(failure=failure):
                self.exercise_mount(failure)

if __name__ == '__main__':
    unittest.main(verbosity=2)
