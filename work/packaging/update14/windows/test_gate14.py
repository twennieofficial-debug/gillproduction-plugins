"""Rejection-path unit tests. Synthetic schemas are never native validation evidence."""
from pathlib import Path
import json,tempfile,unittest
from unittest.mock import patch
import gate14 as g
import build14

class PayloadGateTests(unittest.TestCase):
    def fixture(self,root):
        product={'name':'Fixture','target':'Fixture','version':'0.14.0'}
        module=root/'Fixture.vst3';module.write_bytes(b'synthetic unit fixture, not PE')
        folder=root/'Validation/Fixture';folder.mkdir(parents=True)
        result={'passed':True,'exit_code':0,'strictness':10,'module_unchanged':True,'factory_version_verified':True,
                'module_sha256':g.sha(module),'validator':'Tracktion pluginval 1.0.4'}
        g.write(folder/'result.json',result)
        console='SUCCESS Fixture v0.14.0\n'+''.join(f'Testing with sample rate [{a}] and block size [{b}]\n' for a in (44100,48000,88200,96000,192000) for b in (1,16,64,127,256,512,1024,2048))
        (folder/'console.txt').write_text(console)
        return product,module,folder

    def test_changed_module_rejected_despite_passing_result(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);product,module,_=self.fixture(root)
            with patch.object(g,'BASE',root),patch.object(g,'module',return_value=module):
                g.validation(product)
                module.write_bytes(b'changed payload')
                with self.assertRaisesRegex(RuntimeError,'module binding'):g.validation(product)

    def test_incomplete_rate_buffer_console_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);product,module,folder=self.fixture(root)
            console=folder/'console.txt';console.write_text(console.read_text().replace('Testing with sample rate [192000] and block size [2048]','omitted'))
            with patch.object(g,'BASE',root),patch.object(g,'module',return_value=module),self.assertRaisesRegex(RuntimeError,'matrix incomplete'):
                g.validation(product)

    def test_failed_result_cannot_pass_with_success_console(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);product,module,folder=self.fixture(root)
            result=g.read(folder/'result.json');result['passed']=False;g.write(folder/'result.json',result)
            with patch.object(g,'BASE',root),patch.object(g,'module',return_value=module),self.assertRaisesRegex(RuntimeError,'Strict validation failed'):
                g.validation(product)

    def test_build_receipt_cannot_omit_source_closure(self):
        matrix={'GILLFIXTURE':['DSP']};receipt={'group':'GILLFIXTURE','build_exit':0,'test_exit':0,'source_unchanged':True,'source_files':{}}
        def read(path):return matrix if Path(path).name=='ctest-matrix.json' else receipt
        with patch.object(g,'catalog',return_value=[{'group':'GILLFIXTURE'}]),patch.object(g,'read',side_effect=read),patch.object(g,'expected_group_sources',return_value={'GILLFIXTURE/Source/a.cpp':'hash'}),self.assertRaisesRegex(RuntimeError,'source closure changed'):
            g.native_records()

    def test_sealed_evidence_cannot_be_changed(self):
        with patch.object(g,'read',return_value={'source_sha256':'old'}),patch.object(g,'collect',return_value={'source_sha256':'new'}),self.assertRaisesRegex(RuntimeError,'changed after sealing'):
            g.verified_manifest()

    def test_development_path_refuses_before_any_staging(self):
        with patch.object(g,'baseline_check') as baseline,self.assertRaisesRegex(RuntimeError,'Unverified development'):
            build14.build(development=True)
        baseline.assert_not_called()

    def test_transaction_has_only_allowed_release_identifier_changes(self):
        reuse=g.transaction_reuse()
        self.assertTrue(reuse['only_release_identifiers_changed'])
        self.assertEqual(reuse['previous_checks'],90)
        self.assertFalse(reuse['previous_tests_claimed_as_rerun'])

if __name__=='__main__':unittest.main(verbosity=2)
