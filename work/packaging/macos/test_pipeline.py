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
        pro = live
        if name == "GILLHARMONY":
            live, pro = p.math.ceil(rate * .025) + 80, p.math.ceil(rate * .068) + 112
        if name == "GILLRESCUE":
            live, pro = p.math.ceil(rate * .004), p.math.ceil(rate * .012)
        if name == "GILLCEILING": pro = p.math.ceil(rate*.003)+32
        if name == "GILLWEIGHT": pro = 32
        products.append({"name": name, "factory_version": product["version"], "factory_version_verified": True,
                         "factory_uid": format(index + 1, "x"), "manufacturer": "GILLPRODUCTION",
                         "live_latency_samples": live, "pro_latency_samples": pro})
    return {"passed": True, "failures": 0, "checks": 1, "sample_rate": rate,
            "actual_vst3_bundles": p.PRODUCT_COUNT, "products": products}


def native_quality_fixture(records, arch):
    runs = [{"sample_rate": rate, "exit_code": 0, "result": quality_result_fixture(rate)} for rate in p.QUALITY_SAMPLE_RATES]
    return {"passed": True, "architecture": arch, "source": {"source_sha256": "fixture"}, "plugins": records,
            "quality_host": {"passed": True, "architecture": arch, "source_sha256": "fixture",
                             "executable_architectures": [arch], "executable_sha256": "0" * 64,
                             "runs": runs, "bundle_sha256": {r["name"]: r["bundle_sha256"] for r in records}},
            "mix_host": native_mix_fixture(records, arch)}


def native_mix_fixture(records, arch):
    """Synthetic schema fixture only; no native execution is claimed by these unit tests."""
    products = [{"name": name, "version": "0.7.0", "factory_uid": "abc" if name == "GILLMIX" else "def",
                 "bundle": "/fixture/" + name + ".vst3"} for name in ("GILLMIX", "GILLLINK", "GILLLINK", "GILLLINK")]
    return {"passed": True, "architecture": arch, "source_sha256": "fixture",
            "executable_architectures": [arch], "executable_sha256": "1" * 64,
            "bundle_sha256": {r["name"]: r["bundle_sha256"] for r in records if r["name"] in ("GILLMIX", "GILLLINK")},
            "exit_code": 0, "result": {"passed": True, "checks": 1, "failures": 0, "sample_rate": 48000,
                                        "native_instances": 4, "products": products},
            "report": "test-evidence/MIX_HOST/mix-host-48000.json", "report_sha256": "2" * 64}


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

    def test_quality_and_ctest_pass_cannot_replace_missing_mix_host(self):
        report = p.read(self.universal / "universal-build.json")
        report["native_builds"]["arm64"].pop("mix_host")
        p.write(self.universal / "universal-build.json", report)
        with self.assertRaisesRegex(RuntimeError, "Missing native MIX real-host"):
            p.verify_release_gate(self.universal, self.validation)


class SourceArchiveTests(unittest.TestCase):
    def test_mix_host_build_recipe_is_required_and_fingerprinted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            required = ("GILLCommon/QualityBus.h", "GILLCommon/QualityUi.h", "GILLCommon/MaterialUi.h",
                        "GILLCommon/Tests/QualityHost.cpp", "GILLCommon/Tests/MacQualityHost.mm",
                        "GILLCommon/Tests/QualityHostCMake/CMakeLists.txt", "dependencies/JUCE/CMakeLists.txt",
                        "GILLMIX/CMakeLists.txt", "GILLMIX/Tests/RealHost.cpp", "GILLMIX/Tests/MacRealHost.mm",
                        "GILLMIX/Tests/HostCMake/CMakeLists.txt")
            for relative in required:
                path = root / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("fixture source")
            with patch.object(p, "GROUPS", ["GILLMIX"]), patch.object(p, "CTEST_MATRIX", {"GILLMIX": []}):
                before = p.source_check(root)
                recipe = "GILLMIX/Tests/HostCMake/CMakeLists.txt"
                self.assertIn(recipe, before["files"])
                (root / recipe).write_text("changed host build recipe")
                self.assertNotEqual(before["source_sha256"], p.source_check(root)["source_sha256"])
                (root / recipe).unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native MIX host source"):
                    p.source_check(root)
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
        self.assertEqual(len(p.PRODUCTS), 47)
        self.assertEqual(len({x["code"] for x in p.PRODUCTS}), 47)
        self.assertEqual(sum(x["version"] == "0.6.0" for x in p.PRODUCTS), 22)
        self.assertEqual({x["name"] for x in p.PRODUCTS if x["version"] == "0.7.0"},
                         {"GILLMIX", "GILLLINK", "GILLHARMONY", "GILLREFERENCE", "GILLRESCUE"})
        self.assertEqual(sum(len(tests) for tests in p.CTEST_MATRIX.values()), 58)
        self.assertIn("LIVE_QUALITY_DSP", p.CTEST_MATRIX["GILLNEXT"])
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
        self.assertEqual(sum(len(t) for t in p.CTEST_MATRIX.values()), 58)
        for group, expected in p.CTEST_MATRIX.items():
            p.verify_ctest_listing(group, {"tests": [{"name": name} for name in expected]})

    def test_a_missing_test_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, "missing="):
            p.verify_ctest_listing("GILLNEXT", {"tests": [{"name": name} for name in p.CTEST_MATRIX["GILLNEXT"][:-1]]})

    def test_additional_registered_tests_are_included_without_dropping_required_suites(self):
        tests = [{"name": name} for name in p.CTEST_MATRIX["GILLNEXT"]] + [{"name": "LIVE_CAUSAL_DSP"}]
        p.verify_ctest_listing("GILLNEXT", {"tests": tests})

    def test_original_39_suites_remain_required(self):
        old = {k: [name for name in v if name != "LIVE_QUALITY_DSP"] for k, v in p.CTEST_MATRIX.items()
               if k not in {"GILLCONTROL", "GILLCREATIVE", "GILLSMARTDEESSER", "GILLMIX", "GILLTOOLS", "GILLMASTER"}}
        self.assertEqual(sum(map(len, old.values())), 39)

    def test_all47_release06_native_suites_are_now_explicitly_required(self):
        old = {k: v for k, v in p.CTEST_MATRIX.items() if k not in {"GILLMIX", "GILLTOOLS", "GILLMASTER"}}
        self.assertEqual(sum(map(len, old.values())), 47)

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

    def test_release09_keeps_exact_old_and_new_product_versions(self):
        self.assertEqual((p.RELEASE, p.SUITE_VERSION), ("09", "0.9.0"))
        for rate in p.QUALITY_SAMPLE_RATES:
            p.verify_quality_result(quality_result_fixture(rate), rate)
        for name, wrong in (("GILLEQ", "0.7.0"), ("GILLHARMONY", "0.6.0"),
                            ("GILLREFERENCE", "0.8.0"), ("GILLLINK", ""), ("GILLVOX", "0.6.0"), ("GILLFINISH", "0.7.0"), ("GILLCEILING", "0.8.0"), ("GILLDELIVER", "0.7.0")):
            result = quality_result_fixture(48000)
            next(x for x in result["products"] if x["name"] == name)["factory_version"] = wrong
            with self.subTest(name=name, version=wrong), self.assertRaisesRegex(RuntimeError, "factory version"):
                p.verify_quality_result(result, 48000)

    def test_harmony_and_rescue_exact_reported_delay_at_each_native_rate(self):
        measured_harmony = {44100: (1183, 3111), 48000: (1280, 3377),
                            96000: (2480, 6641), 192000: (4880, 13169)}
        measured_rescue = {44100: (177, 530), 48000: (192, 576),
                           96000: (384, 1152), 192000: (768, 2304)}
        for rate in p.QUALITY_SAMPLE_RATES:
            result = quality_result_fixture(rate)
            for name, measured in (("GILLHARMONY", measured_harmony), ("GILLRESCUE", measured_rescue)):
                product = next(x for x in result["products"] if x["name"] == name)
                self.assertEqual((product["live_latency_samples"], product["pro_latency_samples"]), measured[rate])
                for field in ("live_latency_samples", "pro_latency_samples"):
                    for delta in (-1, 1):
                        bad = copy.deepcopy(result)
                        next(x for x in bad["products"] if x["name"] == name)[field] += delta
                        with self.subTest(rate=rate, name=name, field=field, delta=delta), self.assertRaisesRegex(RuntimeError, "latency differs"):
                            p.verify_quality_result(bad, rate)
            p.verify_quality_result(result, rate)

    def test_new_gain_and_monitor_paths_require_zero_in_both_modes(self):
        for name in ("GILLMIX", "GILLLINK", "GILLREFERENCE"):
            for live, pro in ((0, 1), (1, 1)):
                result = quality_result_fixture(48000)
                next(x for x in result["products"] if x["name"] == name).update(
                    live_latency_samples=live, pro_latency_samples=pro)
                with self.subTest(name=name, live=live, pro=pro), self.assertRaisesRegex(RuntimeError, "zero latency in both modes"):
                    p.verify_quality_result(result, 48000)

    def test_live_quality_suite_cannot_be_silently_removed(self):
        names = [name for name in p.CTEST_MATRIX["GILLNEXT"] if name != "LIVE_QUALITY_DSP"]
        with self.assertRaisesRegex(RuntimeError, "LIVE_QUALITY_DSP"):
            p.verify_ctest_listing("GILLNEXT", {"tests": [{"name": name} for name in names]})

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

    def test_crash_collection_is_fresh_and_only_for_quality_host(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); reports = root / "reports"; reports.mkdir()
            fresh = reports / "GillQualityHost-current.ips"
            stale = reports / "GillQualityHost-old.crash"
            other = reports / "OtherApplication-current.ips"
            for path in (fresh, stale, other): path.write_text("diagnostic fixture")
            old = time.time_ns() - 10_000_000_000
            p.os.utime(stale, ns=(old, old))
            copied = p.collect_quality_crashes([reports], root / "collected", time.time_ns() - 3_000_000_000)
            self.assertEqual(copied, [fresh.name])
            self.assertEqual([path.name for path in (root / "collected").iterdir()], [fresh.name])

    def test_lldb_uses_crash_hooks_without_changing_the_original_gate(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "logs").mkdir()
            with patch.object(p.sys, "platform", "darwin"), \
                    patch.object(p, "collect_quality_crashes", return_value=[]), \
                    patch.object(p.subprocess, "run") as run:
                run.return_value.returncode = 1
                result = p.diagnose_quality_failure(root / "host", root / "paths.json", 48000, root, 0, True)
            command = run.call_args.args[0]
            hooks = [command[index + 1] for index, value in enumerate(command[:-1]) if value == "-k"]
            self.assertEqual(hooks, ["thread backtrace all", "image list -o -f"])
            self.assertTrue(result["diagnostic_only"])
            self.assertEqual(result["lldb_exit_code"], 1)
            self.assertNotIn("passed", result)


class NativeMixGateTests(unittest.TestCase):
    def setUp(self):
        records = [{"name": name, "bundle_sha256": "3" * 64} for name in ("GILLMIX", "GILLLINK")]
        self.report = {"architecture": "arm64", "source": {"source_sha256": "fixture"}, "plugins": records,
                       "mix_host": native_mix_fixture(records, "arm64")}

    def test_complete_native_mix_gate_passes(self):
        p.verify_native_mix_gate(self.report)

    def test_wrong_architecture_source_exit_and_hash_cannot_pass(self):
        for field, value in (("architecture", "x86_64"), ("source_sha256", "other"), ("exit_code", 1),
                             ("executable_architectures", ["x86_64"]), ("bundle_sha256", {})):
            report = copy.deepcopy(self.report); report["mix_host"][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                p.verify_native_mix_gate(report)

    def test_missing_failed_or_wrong_instance_results_rejected(self):
        for field, value in (("passed", False), ("checks", 0), ("failures", 1), ("sample_rate", 44100), ("native_instances", 2)):
            report = copy.deepcopy(self.report); report["mix_host"]["result"][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                p.verify_native_mix_gate(report)

    def test_wrong_factory_version_or_instance_list_rejected(self):
        for change in ("version", "name", "count", "uid"):
            report = copy.deepcopy(self.report); products = report["mix_host"]["result"]["products"]
            if change == "version": products[1]["version"] = "0.6.0"
            if change == "name": products[1]["name"] = "GILLMIX"
            if change == "count": products.pop()
            if change == "uid": products[1]["factory_uid"] = "abc"
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                p.verify_native_mix_gate(report)

    def test_a_different_loaded_bundle_is_rejected(self):
        result = self.report["mix_host"]["result"]
        paths = {name: Path("/fixture") / (name + ".vst3") for name in ("GILLMIX", "GILLLINK")}
        p.verify_mix_result(result, paths)
        result["products"][2]["bundle"] = "/other/GILLLINK.vst3"
        with self.assertRaisesRegex(RuntimeError, "different bundle"):
            p.verify_mix_result(result, paths)

    def test_report_bytes_are_bound_at_merge(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); gate = self.report["mix_host"]
            path = root / gate["report"]; p.write(path, gate["result"]); gate["report_sha256"] = p.sha(path)
            p.verify_native_mix_gate(self.report, root)
            path.write_text("{}")
            with self.assertRaisesRegex(RuntimeError, "report bytes changed"):
                p.verify_native_mix_gate(self.report, root)


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
