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


def quality_result_fixture(rate):
    """Synthetic schema input for rejection tests, never native test evidence."""
    products = []
    for index, product in enumerate(p.PRODUCTS):
        name = product["name"]
        live = p.math.ceil(rate * .016) if name in ("GILLTUNE", "GILLTUNE LIVE") else int(rate * .02) if name == "GILLFORM" else 0
        products.append({"name": name, "factory_version": product["version"], "factory_version_verified": True,
                         "factory_uid": format(index + 1, "x"), "manufacturer": "GILLPRODUCTION",
                         "live_latency_samples": live, "pro_latency_samples": live})
    return {"passed": True, "failures": 0, "checks": 1, "sample_rate": rate,
            "actual_vst3_bundles": p.PRODUCT_COUNT, "products": products}


def native_quality_fixture(records, arch):
    runs = [{"sample_rate": rate, "exit_code": 0, "result": quality_result_fixture(rate)} for rate in p.QUALITY_SAMPLE_RATES]
    return {"passed": True, "architecture": arch, "source": {"source_sha256": "fixture"}, "plugins": records,
            "quality_host": {"passed": True, "architecture": arch, "source_sha256": "fixture",
                             "executable_architectures": [arch], "executable_sha256": "0" * 64,
                             "runs": runs, "bundle_sha256": {r["name"]: r["bundle_sha256"] for r in records}}}


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
        p.write(self.universal / "universal-build.json", {"passed": True, "plugins": self.records, "source_sha256": "fixture",
            "native_builds": {arch: native_quality_fixture(self.records, arch) for arch in ("arm64", "x86_64")}})
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
        self.assertEqual(len(self.gate()["plugins"]), p.PRODUCT_COUNT)

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
    def test_generated_audio_does_not_change_source_hash_but_licensed_input_does(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for relative in ("GILLCommon/QualityBus.h", "GILLCommon/QualityUi.h", "GILLCommon/MaterialUi.h",
                             "GILLCommon/Tests/QualityHost.cpp", "GILLCommon/Tests/MacQualityHost.mm",
                             "GILLCommon/Tests/QualityHostCMake/CMakeLists.txt", "dependencies/JUCE/CMakeLists.txt",
                             "GILLDEREVERB/CMakeLists.txt", "GILLDEESSER/CMakeLists.txt"):
                path = root / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("source fixture")
            licensed = root / "GILLDEREVERB/Tests/fixtures/dry_48k.wav"
            outputs = [root / "GILLDEREVERB/Tests/fixtures/processed_dry_48k.wav",
                       root / "GILLDEREVERB/Tests/latency-v03-baseline/fixtures/wet_48k.wav",
                       root / "GILLDEESSER/Tests/fixtures/synthetic.wav"]
            code = root / "GILLDEREVERB/Tests/fixtures/processed_regression.cpp"
            for path in [licensed, code] + outputs:
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"initial fixture")
            with patch.object(p, "GROUPS", ["GILLDEREVERB", "GILLDEESSER"]), \
                 patch.object(p, "CTEST_MATRIX", {"GILLDEREVERB": [], "GILLDEESSER": []}):
                before = p.source_check(root)
                self.assertIn(code.relative_to(root).as_posix(), before["files"])
                for path in outputs:
                    self.assertNotIn(path.relative_to(root).as_posix(), before["files"])
                    path.write_bytes(b"regenerated test output")
                self.assertEqual(before["source_sha256"], p.source_check(root)["source_sha256"])
                licensed.write_bytes(b"changed licensed input")
                self.assertNotEqual(before["source_sha256"], p.source_check(root)["source_sha256"])

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

    def test_shared_quality_sources_cannot_be_omitted(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "source.zip"
            manifest = {"files": {"GILLCommon/QualityBus.h": p.hashlib.sha256(b"shared").hexdigest()}}
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("work/dependencies/JUCE/CMakeLists.txt", "JUCE")
                archive.writestr("work/dependencies/JUCE/LICENSE.md", "license")
            with self.assertRaisesRegex(RuntimeError, "GILLCommon/QualityBus.h"):
                p.verify_source_archive(path, manifest)

    def test_catalog_preserves_products_and_compact_sizes(self):
        self.assertEqual(len(p.PRODUCTS), 34)
        self.assertEqual(len({x["code"] for x in p.PRODUCTS}), 34)
        for product in p.PRODUCTS:
            self.assertLessEqual(product["default_size"][0], 900)
            self.assertLessEqual(product["default_size"][1], 580)


class NativeEvidenceTests(unittest.TestCase):
    def test_live_failure_excerpt_keeps_first_last_failures_and_final_summary(self):
        lines = ["PASS " + str(i) for i in range(2000)]
        lines[10] = "FAIL important first measurement"
        lines[1900] = "FAILED important late measurement"
        lines[-1] = "The following tests FAILED: DYNAMICS_REALTIME"
        excerpt = p.ctest_failure_excerpt("\n".join(lines))
        self.assertIn("important first measurement", excerpt)
        self.assertIn("important late measurement", excerpt)
        self.assertIn("DYNAMICS_REALTIME", excerpt)
        self.assertLess(len(excerpt), 18000)
        self.assertNotIn("PASS 1000\n", excerpt)

    def test_live_failure_excerpt_is_bounded_for_very_noisy_failures(self):
        excerpt = p.ctest_failure_excerpt("\n".join("FAIL " + str(i) + " x" * 500 for i in range(10000)))
        self.assertLessEqual(len(excerpt), 18000)
        self.assertIn("FAIL 0 ", excerpt)
        self.assertIn("FAIL 9999 ", excerpt)

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
            self.assertEqual(len(failure["completed_groups"]), len(p.GROUPS) - 1)
            self.assertEqual(len(failure["failed_groups"]), 1)

    def test_all_expected_ctest_entries_match(self):
        self.assertEqual(sum(len(t) for t in p.CTEST_MATRIX.values()), 46)
        for group, expected in p.CTEST_MATRIX.items():
            p.verify_ctest_listing(group, {"tests": [{"name": name} for name in expected]})

    def test_a_missing_test_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, "missing="):
            p.verify_ctest_listing("GILLNEXT", {"tests": [{"name": name} for name in p.CTEST_MATRIX["GILLNEXT"][:-1]]})

    def test_additional_registered_tests_are_included_without_dropping_required_suites(self):
        tests = [{"name": name} for name in p.CTEST_MATRIX["GILLNEXT"]] + [{"name": "LIVE_CAUSAL_DSP"}]
        p.verify_ctest_listing("GILLNEXT", {"tests": tests})

    def test_original_39_suites_remain_required(self):
        old = {k: v for k, v in p.CTEST_MATRIX.items() if k not in {"GILLCONTROL", "GILLCREATIVE", "GILLSMARTDEESSER"}}
        self.assertEqual(sum(map(len, old.values())), 39)

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
            old_time = time.time_ns() - 10_000_000_000
            p.os.utime(old, ns=(old_time, old_time))
            # Data-drive filesystems may round timestamps. This still keeps
            # the old fixture outside the fresh window without a 1970 date.
            start = time.time_ns() - 3_000_000_000
            new = group / "Tests/GILLEQ-UI-860.png"
            new.write_bytes(b"new screenshot fixture")
            details = root / "build/Testing/Temporary/LastTest.log"
            details.parent.mkdir(parents=True)
            details.write_text("Test failed; preserve diagnostic data")
            p.collect_test_evidence(group, root / "build", root / "results", start)
            self.assertEqual((root / "results/test-evidence/GILLEQ/GILLEQ-UI-860.png").read_bytes(), new.read_bytes())
            self.assertFalse((root / "results/test-evidence/GILLEQ/old.png").exists())
            self.assertIn("Test failed", (root / "results/logs/GILLEQ-LastTest.log").read_text())

    def test_new_group_build_directory_screenshot_is_collected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); build = root / "build"; build.mkdir()
            start = time.time_ns() - 3_000_000_000
            screenshot = build / "GILLCONTROL-UI-Compact.png"
            screenshot.write_bytes(b"fresh image")
            (build / "CMakeCache.txt").write_text("not test evidence")
            p.collect_test_evidence(root / "GILLCONTROL", build, root / "results", start)
            self.assertTrue((root / "results/test-evidence/GILLCONTROL" / screenshot.name).is_file())
            self.assertFalse((root / "results/test-evidence/GILLCONTROL/CMakeCache.txt").exists())


class NativeQualityGateTests(unittest.TestCase):
    def setUp(self):
        self.records = [{"name": product["name"], "bundle_sha256": "1" * 64} for product in p.PRODUCTS]
        self.report = native_quality_fixture(self.records, "arm64")

    def test_all_four_rates_and_exact_catalog_required(self):
        p.verify_native_quality_gate(self.report)
        self.report["quality_host"]["runs"].pop()
        with self.assertRaisesRegex(RuntimeError, "four native sample rates"):
            p.verify_native_quality_gate(self.report)

    def test_missing_gate_rejected_even_if_native_ctests_passed(self):
        del self.report["quality_host"]
        with self.assertRaisesRegex(RuntimeError, "Missing native QualityHost"):
            p.verify_native_quality_gate(self.report)

    def test_wrong_architecture_cannot_substitute_for_native_host(self):
        self.report["quality_host"]["executable_architectures"] = ["x86_64"]
        with self.assertRaisesRegex(RuntimeError, "native executable"):
            p.verify_native_quality_gate(self.report)

    def test_failed_real_process_cannot_be_overridden_by_json_pass(self):
        self.report["quality_host"]["runs"][0]["exit_code"] = 1
        with self.assertRaisesRegex(RuntimeError, "execution did not succeed"):
            p.verify_native_quality_gate(self.report)

    def test_changed_bundle_hash_rejected(self):
        self.report["quality_host"]["bundle_sha256"][p.PRODUCTS[0]["name"]] = "2" * 64
        with self.assertRaisesRegex(RuntimeError, "different native bundles"):
            p.verify_native_quality_gate(self.report)

    def test_omitted_product_and_old_factory_version_rejected(self):
        result = quality_result_fixture(48000)
        result["products"].pop()
        with self.assertRaisesRegex(RuntimeError, "product list"):
            p.verify_quality_result(result, 48000)
        result = quality_result_fixture(48000)
        result["products"][0]["factory_version"] = "0.5.0"
        with self.assertRaisesRegex(RuntimeError, "factory version"):
            p.verify_quality_result(result, 48000)

    def test_nonzero_live_and_invalid_pitch_latency_rejected(self):
        for name, latency in (("GILLSMARTDEESSER", 1), ("GILLTUNE", 0), ("GILLFORM", 1200)):
            result = quality_result_fixture(48000)
            product = next(x for x in result["products"] if x["name"] == name)
            product.update(live_latency_samples=latency, pro_latency_samples=max(latency, 2000))
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                p.verify_quality_result(result, 48000)

    def test_duplicate_factory_identity_rejected(self):
        result = quality_result_fixture(48000)
        result["products"][1]["factory_uid"] = result["products"][0]["factory_uid"]
        with self.assertRaisesRegex(RuntimeError, "identity invalid or duplicated"):
            p.verify_quality_result(result, 48000)

    def test_changed_report_bytes_rejected_at_merge(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for row in self.report["quality_host"]["runs"]:
                path = root / f'{row["sample_rate"]}.json'
                p.write(path, row["result"])
                row.update(report=path.name, report_sha256=p.sha(path))
            p.verify_native_quality_gate(self.report, root)
            (root / "48000.json").write_text("{}")
            with self.assertRaisesRegex(RuntimeError, "report bytes changed"):
                p.verify_native_quality_gate(self.report, root)


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
