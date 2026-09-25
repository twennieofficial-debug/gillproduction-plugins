"""Local packaging-gate regression tests; these are not native Mac DSP tests."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
