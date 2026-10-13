"""Portable runner-contract and deliberately corrupted-evidence checks."""
import copy
import tempfile
import unittest
from pathlib import Path
import run

class Contract(unittest.TestCase):
    def test_frozen_baseline(self):
        run.validate_baseline()

    def test_config_matrix(self):
        for name in run.PRODUCTS:
            rows = run.host_config(name)
            self.assertEqual(len({r['id'] for r in rows}), len(rows))
            self.assertEqual(sum(r['mode']=='silent' for r in rows),8)
            self.assertEqual(sum(r['mode']=='left' for r in rows),4)
            self.assertEqual(sum(r['mode']=='right' for r in rows),4)
            self.assertEqual({r['quality'] for r in rows},{'LIVE','PRO'})

    def test_escape_and_empty_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            self.assertEqual(run.safe_path(root,'inside'),root/'inside')
            with self.assertRaises(RuntimeError):run.safe_path(root,'../outside')
            for name in run.PRODUCTS:
                with self.assertRaises(RuntimeError):run.validate_host(name,[],root)

    def test_expected_failure_is_not_success(self):
        import sys
        with tempfile.TemporaryDirectory() as directory:
            runner=run.Runner(Path(directory))
            runner.run('valid',[sys.executable,'-c','print("positive")'],token='positive')
            runner.run('negative',[sys.executable,'-c','import sys;print("defect");sys.exit(1)'],1,'defect')
            with self.assertRaises(RuntimeError):runner.run('wrong-exit',[sys.executable,'-c','print("defect")'],1,'defect')
            with self.assertRaises(RuntimeError):runner.run('wrong-token',[sys.executable,'-c','print("other")'],0,'positive')
            self.assertTrue(all(len(c['executable_sha256'])==64 for c in runner.commands))

    def test_no_config_parameters_missing(self):
        for name in run.PRODUCTS:
            for c in run.host_config(name):
                self.assertTrue(c['settings'])
                self.assertIn(c['main'],[1,2])
                self.assertGreater(c['rate'],0)

    def test_transport_manifest_content_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'test').write_bytes(b'content')
            manifest=run.bundle_tree(root);manifest['test']['executable']=False
            self.assertEqual(run.bundle_tree(root,manifest),manifest)
            bad=copy.deepcopy(manifest);bad['test']['sha256']='0'*64
            with self.assertRaises(RuntimeError):run.bundle_tree(root,bad)
            (root/'extra').write_text('unexpected')
            with self.assertRaises(RuntimeError):run.bundle_tree(root,manifest)

    def test_state_false_success_rejected(self):
        with self.assertRaises(RuntimeError):run.validate_state({'passed':True,'finite':False},'fake')
        state=dict(passed=True,finite=True,layoutApplied=True,failures=0,checks=8,twoStepParameters=1,parameters=[dict(checks=[dict(pass_=True)])])
        with self.assertRaises(RuntimeError):run.validate_state(state,'fake')

if __name__=='__main__':unittest.main()
