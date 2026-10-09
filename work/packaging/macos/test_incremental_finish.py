"""Offline safety tests. Positive fixtures never claim actual native execution."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import incremental_finish as inc
import pipeline as p
from test_incremental_texture import IdentityTests, DownloadTests


def redigest(report):
    report['source_sha256'] = p.digest_manifest(report['files'])


class FinishSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.shared = self.root / 'GILLCommon/LiveCausal.h'
        self.shared.parent.mkdir(parents=True)
        self.shared.write_bytes(b'shared source\n')
        self.old = {'products': p.PRODUCT_COUNT, 'groups': p.GROUPS, 'files': {
            'GILLCommon/LiveCausal.h': p.sha(self.shared), 'GILLFINISH/Source/Spectral.h': 'a'*64,
            'JUCE_PIN': p.JUCE_COMMIT, 'PRODUCT_CATALOG': 'b'*64, 'CTEST_MATRIX': 'c'*64}}
        redigest(self.old)
        self.new = copy.deepcopy(self.old)
        self.new['files']['GILLFINISH/Source/Spectral.h'] = 'd'*64
        redigest(self.new)

    def check(self, matrix=None):
        return inc.verify_source_closure(self.old, self.new, self.root, matrix)

    def test_valid_finish_only_closure(self):
        result = self.check()
        self.assertEqual(result['changed_files'], ['GILLFINISH/Source/Spectral.h'])
        self.assertFalse(result['release_approved'])
        self.assertEqual(result['unchanged_files']['GILLCommon/LiveCausal.h'], p.sha(self.shared))

    def test_shared_edit_requires_full_rebuild_even_with_finish_edit(self):
        self.new['files']['GILLCommon/LiveCausal.h'] = 'e'*64
        redigest(self.new)
        with self.assertRaisesRegex(RuntimeError, 'FULL_REBUILD'): self.check()

    def test_current_bytes_must_equal_manifest(self):
        self.shared.write_bytes(b'mutated actual bytes')
        with self.assertRaisesRegex(RuntimeError, 'bytes missing or differ'): self.check()

    def test_wrong_count_hash_path_noop(self):
        for mutation in ('count','hash','path','noop','catalog','juce','matrix'):
            with self.subTest(mutation=mutation):
                new = copy.deepcopy(self.new)
                if mutation == 'count': new['products'] -= 1
                if mutation == 'hash': new['source_sha256'] = '0'*64
                if mutation == 'path': new['files']['GILLFINISH/../elsewhere'] = 'f'*64; redigest(new)
                if mutation == 'noop': new = self.old
                if mutation == 'catalog': new['files']['PRODUCT_CATALOG'] = 'f'*64; redigest(new)
                if mutation == 'juce': new['files']['JUCE_PIN'] = 'f'*40; redigest(new)
                if mutation == 'matrix': new['files']['CTEST_MATRIX'] = 'f'*64; redigest(new)
                with self.assertRaises(RuntimeError): inc.verify_source_closure(self.old,new,self.root)

    def test_reverse_dependency_rejected(self):
        self.shared.write_bytes(b'#include "../GILLFINISH/Source/Spectral.h"')
        for report in (self.old,self.new): report['files']['GILLCommon/LiveCausal.h'] = p.sha(self.shared); redigest(report)
        with self.assertRaisesRegex(RuntimeError,'references FINISH'): self.check()

    def test_matrix_only_adds_finish_tests(self):
        old = copy.deepcopy(p.CTEST_MATRIX)
        old['GILLFINISH'] = sorted(inc.REQUIRED_FINISH_TESTS)
        new = copy.deepcopy(old); new['GILLFINISH'].append('SilkMoving_DSP')
        matrix = inc.validate_matrix_delta(json.dumps(old).encode(),json.dumps(new).encode())
        self.old['files']['CTEST_MATRIX'] = matrix['base_sha256']; redigest(self.old)
        self.new['files']['CTEST_MATRIX'] = matrix['current_sha256']; redigest(self.new)
        self.check(matrix)
        for group in ('GILLFINISH','GILLEQ'):
            bad = copy.deepcopy(new); bad[group].pop(0)
            with self.assertRaises(RuntimeError): inc.validate_matrix_delta(json.dumps(old).encode(),json.dumps(bad).encode())
        bad = copy.deepcopy(new); bad['GILLFINISH'].append('SilkMoving_DSP')
        with self.assertRaises(RuntimeError): inc.validate_matrix_delta(json.dumps(old).encode(),json.dumps(bad).encode())

    def test_changed_recipe_rejected(self):
        data = {}
        for relative in inc.RECIPES:
            path = self.root / relative; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(b'recipe\n'); data[relative]=b'recipe\n'
        path = self.root / inc.MATRIX; path.write_text(json.dumps(p.CTEST_MATRIX)); data[inc.MATRIX]=path.read_bytes()
        inc.verify_recipes(self.root,'a'*40,lambda path,commit:data[path])
        for relative in inc.RECIPES:
            with self.subTest(relative=relative):
                (self.root/relative).write_bytes(b'recipe\r\n')
                with self.assertRaisesRegex(RuntimeError,'FULL_REBUILD'): inc.verify_recipes(self.root,'a'*40,lambda path,commit:data[path])
                (self.root/relative).write_bytes(data[relative])

    def test_base_wrong_source_failed_and_count(self):
        for base in ({'passed':False},{'passed':True,'architecture':'arm64','source':{'source_sha256':'wrong'}},
                     {'passed':True,'architecture':'arm64','source':{'source_sha256':'a'*64},'plugins':[]}):
            with self.assertRaises(RuntimeError): inc.verify_base_report(base,'a'*64,'arm64',self.root,p.CTEST_MATRIX)

    def test_actual_binary_and_modes_checked_before_acceptance(self):
        records=[dict(name=x['name'],bundle_sha256='original') for x in p.PRODUCTS]
        report={'passed':True,'architecture':'arm64','source':{'source_sha256':'a'*64},'plugins':records}
        for mismatch in ('Mach-O','executable modes','bundle hash'):
            with self.subTest(mismatch=mismatch), patch.object(p,'inspect_bundle',return_value={'name':p.PRODUCTS[0]['name'],'bundle_sha256':mismatch}) as inspect:
                with self.assertRaisesRegex(RuntimeError,'Mach-O'): inc.verify_base_report(report,'a'*64,'arm64',self.root,p.CTEST_MATRIX)
                inspect.assert_called_once()


if __name__ == '__main__': unittest.main()
