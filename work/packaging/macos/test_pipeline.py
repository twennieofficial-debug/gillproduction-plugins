"""Local packaging-gate regression tests; these are not native Mac DSP tests."""
import copy
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import zipfile

import pipeline as p


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.universal = self.root / "universal"
        self.validation = self.root / "validation"
        self.records = []
        for product in p.PRODUCTS:
            bundle = self.universal / "plugins" / (product["name"] + ".vst3")
            bundle.mkdir(parents=True)
            (bundle / "fixture.txt").write_text(product["name"])
            self.records.append({"name": product["name"], "bundle_sha256": p.bundle_digest(bundle), "passed": True, "strictness": 10})
        p.write(self.universal / "universal-build.json", {"passed": True, "plugins": self.records})
        for arch in ("arm64", "x86_64"):
            p.write(self.validation / arch / f"validation-{arch}.json", {
                "passed": True, "native_architecture": arch,
                "universal_report_sha256": p.sha(self.universal / "universal-build.json"),
                "plugins": self.records})

    def tearDown(self):
        self.temp.cleanup()

    def gate(self):
        return p.verify_release_gate(self.universal, self.validation)

    def mutate(self, operation, arch="arm64"):
        path = self.validation / arch / f"validation-{arch}.json"
        value = p.read(path)
        operation(value)
        p.write(path, value)

    def test_matching_both_architecture_reports_pass(self):
        self.assertEqual(len(self.gate()["plugins"]), 29)

    def test_binary_mutation_rejected(self):
        (self.universal / "plugins/GILLRIDE.vst3/fixture.txt").write_text("changed")
        with self.assertRaisesRegex(RuntimeError, "no longer matches"):
            self.gate()

    def test_missing_intel_report_rejected(self):
        (self.validation / "x86_64/validation-x86_64.json").unlink()
        with self.assertRaises(FileNotFoundError):
            self.gate()

    def test_arm_report_cannot_stand_in_for_intel(self):
        self.mutate(lambda r: r.update(native_architecture="arm64"), "x86_64")
        with self.assertRaisesRegex(RuntimeError, "Native x86_64"):
            self.gate()

    def test_failed_report_rejected(self):
        self.mutate(lambda r: r.update(passed=False))
        with self.assertRaises(RuntimeError):
            self.gate()

    def test_wrong_build_report_rejected(self):
        self.mutate(lambda r: r.update(universal_report_sha256="different"))
        with self.assertRaisesRegex(RuntimeError, "another build"):
            self.gate()

    def test_missing_product_rejected(self):
        self.mutate(lambda r: r["plugins"].pop())
        with self.assertRaisesRegex(RuntimeError, "Incomplete"):
            self.gate()

    def test_lower_validation_level_rejected(self):
        self.mutate(lambda r: r["plugins"][0].update(strictness=5))
        with self.assertRaises(RuntimeError):
            self.gate()

    def test_existing_output_not_overwritten(self):
        with self.assertRaisesRegex(RuntimeError, "new/empty"):
            p.new_destination(self.universal)

    def test_windows_cannot_claim_mac_run(self):
        if p.sys.platform == "darwin":
            self.skipTest("This rejection check is for non-Mac development")
        with self.assertRaisesRegex(RuntimeError, "real macOS"):
            p.native_arch()


class SourceArchiveTests(unittest.TestCase):
    def test_current_sources_required_in_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = b"current source"
            manifest = {"files": {"GILLRIDE/Source/test.cpp": p.hashlib.sha256(source).hexdigest()}}
            path = root / "source.zip"
            def archive(code):
                with zipfile.ZipFile(path, "w") as z:
                    z.writestr("work/GILLRIDE/Source/test.cpp", code)
                    z.writestr("work/dependencies/JUCE/CMakeLists.txt", "JUCE")
                    z.writestr("work/dependencies/JUCE/LICENSE.md", "license")
            archive(source)
            p.verify_source_archive(path, manifest)
            archive(b"stale source")
            with self.assertRaisesRegex(RuntimeError, "mismatch"):
                p.verify_source_archive(path, manifest)

    def test_catalog_preserves_products_and_compact_sizes(self):
        self.assertEqual(len(p.PRODUCTS), 29)
        self.assertEqual(len({x["code"] for x in p.PRODUCTS}), 29)
        for product in p.PRODUCTS:
            self.assertLessEqual(product["default_size"][0], 900)
            self.assertLessEqual(product["default_size"][1], 580)


class NativeEvidenceTests(unittest.TestCase):
    def test_failed_group_does_not_hide_later_groups_or_create_success(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            visited = []
            def fake_group(group, *args):
                visited.append(group)
                if group == p.GROUPS[0]:
                    raise RuntimeError("deliberate first-group failure")
                return {"group": group, "passed": True}
            with patch.object(p, "native_arch", return_value="arm64"), \
                 patch.object(p, "source_check", return_value={"source_sha256": "fixture"}), \
                 patch.object(p, "build_group", side_effect=fake_group):
                with self.assertRaisesRegex(RuntimeError, "1 native source groups failed"):
                    p.build(SimpleNamespace(source=root / "source", build_root=root / "build", destination=root / "result", jobs=2))
            self.assertEqual(visited, p.GROUPS)
            self.assertFalse((root / "result/native-build.json").exists())
            failure = p.read(root / "result/native-failure.json")
            self.assertFalse(failure["passed"])
            self.assertEqual(len(failure["completed_groups"]), 8)
            self.assertEqual(len(failure["failed_groups"]), 1)

    def test_all_expected_ctest_entries_match(self):
        self.assertEqual(sum(len(t) for t in p.CTEST_MATRIX.values()), 39)
        for group, expected in p.CTEST_MATRIX.items():
            p.verify_ctest_listing(group, {"tests": [{"name": name} for name in expected]})

    def test_a_missing_test_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, "missing="):
            p.verify_ctest_listing("GILLNEXT", {"tests": [{"name": name} for name in p.CTEST_MATRIX["GILLNEXT"][:-1]]})

    def test_a_duplicate_test_cannot_replace_missing_test(self):
        tests = [{"name": name} for name in p.CTEST_MATRIX["GILLNEXT"]]
        tests[-1] = dict(tests[0])
        with self.assertRaisesRegex(RuntimeError, "missing="):
            p.verify_ctest_listing("GILLNEXT", {"tests": tests})

    def test_failed_test_run_retains_fresh_png_and_ctest_details(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            group = root / "GILLEQ"
            (group / "Tests").mkdir(parents=True)
            old = group / "Tests/old.png"
            old.write_bytes(b"old screenshot fixture")
            p.os.utime(old, ns=(1, 1))
            start = time.time_ns()
            new = group / "Tests/GILLEQ-UI-860.png"
            new.write_bytes(b"new screenshot fixture")
            details = root / "build/Testing/Temporary/LastTest.log"
            details.parent.mkdir(parents=True)
            details.write_text("Test failed; preserve diagnostic data")
            p.collect_test_evidence(group, root / "build", root / "results", start)
            self.assertEqual((root / "results/test-evidence/GILLEQ/GILLEQ-UI-860.png").read_bytes(), new.read_bytes())
            self.assertFalse((root / "results/test-evidence/GILLEQ/old.png").exists())
            self.assertIn("Test failed", (root / "results/logs/GILLEQ-LastTest.log").read_text())


class InstallerEnvironmentTests(unittest.TestCase):
    def test_local_mac_is_not_allowed(self):
        with patch.object(p.sys, "platform", "darwin"), patch.dict(p.os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "disposable GitHub-hosted"):
                p.require_disposable_install_runner("/tmp/test.pkg")

    def test_self_hosted_runner_is_not_allowed(self):
        with patch.object(p.sys, "platform", "darwin"), patch.dict(p.os.environ, {
                "GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "self-hosted", "RUNNER_OS": "macOS"}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "disposable GitHub-hosted"):
                p.require_disposable_install_runner("/tmp/test.pkg")

    def test_only_package_inside_disposable_runner_temp_is_allowed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            with patch.object(p.sys, "platform", "darwin"), patch.dict(p.os.environ, {
                    "GITHUB_ACTIONS": "true", "RUNNER_ENVIRONMENT": "github-hosted", "RUNNER_OS": "macOS", "RUNNER_TEMP": str(root)}, clear=True):
                p.require_disposable_install_runner(root / "delivery/test.pkg")
                with self.assertRaisesRegex(RuntimeError, "inside the GitHub runner"):
                    p.require_disposable_install_runner(root.parent / "outside.pkg")


if __name__ == "__main__":
    unittest.main(verbosity=2)
