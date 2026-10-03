"""Rejection tests for source/artifact reuse; these never claim native execution."""
import copy
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import incremental_texture as inc
import pipeline as p

COMMIT = "2" * 40
SOURCE_SHA = "3" * 64
RUN = 37134180954


def metadata(arch="arm64"):
    run = {"id": RUN, "head_sha": COMMIT, "repository": {"full_name": inc.REPOSITORY},
           "head_repository": {"full_name": inc.REPOSITORY}, "event": "workflow_dispatch",
           "path": ".github/workflows/build-macos.yml", "status": "completed", "conclusion": "cancelled"}
    jobs = [{"name": name, "status": "completed", "conclusion": "success"} for name in inc.NATIVE_JOBS.values()]
    artifact = {"id": 123, "name": "native-" + arch, "expired": False, "digest": "sha256:" + "a" * 64,
                "workflow_run": {"id": RUN, "head_sha": COMMIT}}
    return run, jobs, artifact


def source_fixture():
    files = {"GILLCommon/QualityBus.h": "a" * 64, "GILLEQ/Source/Engine.h": "b" * 64,
             "GILLTEXTURE/Source/PluginEditor.cpp": "c" * 64,
             "GILLTEXTURE/Source/TextureDSP.h": "d" * 64,
             "JUCE_PIN": p.JUCE_COMMIT, "PRODUCT_CATALOG": "e" * 64, "CTEST_MATRIX": "f" * 64}
    old = {"products": 60, "groups": p.GROUPS, "files": files, "source_sha256": p.digest_manifest(files)}
    new = copy.deepcopy(old)
    new["files"]["GILLTEXTURE/Source/PluginEditor.cpp"] = "9" * 64
    new["source_sha256"] = p.digest_manifest(new["files"])
    return old, new


def redigest(source):
    source["source_sha256"] = p.digest_manifest(source["files"])


class IdentityTests(unittest.TestCase):
    def test_valid_identity(self):
        inc.validate_inputs(inc.REPOSITORY, RUN, COMMIT, SOURCE_SHA)

    def test_invalid_identity_rejected(self):
        for values in (("foreign/repo", RUN, COMMIT, SOURCE_SHA), (inc.REPOSITORY, 0, COMMIT, SOURCE_SHA),
                       (inc.REPOSITORY, RUN, "2937ec5", SOURCE_SHA), (inc.REPOSITORY, RUN, COMMIT, "")):
            with self.subTest(values=values), self.assertRaises(RuntimeError):
                inc.validate_inputs(*values)

    def test_cancelled_overall_run_with_two_successful_native_jobs_is_allowed(self):
        inc.validate_remote_metadata(*metadata(), "arm64", RUN, COMMIT)

    def test_each_successful_native_job_is_mandatory(self):
        for index in (0, 1):
            for conclusion in ("failure", "cancelled", ""):
                run, jobs, artifact = metadata()
                jobs[index]["conclusion"] = conclusion
                with self.subTest(index=index, conclusion=conclusion), self.assertRaises(RuntimeError):
                    inc.validate_remote_metadata(run, jobs, artifact, "arm64", RUN, COMMIT)

    def test_duplicate_native_job_rejected(self):
        run, jobs, artifact = metadata(); jobs.append(copy.deepcopy(jobs[0]))
        with self.assertRaises(RuntimeError): inc.validate_remote_metadata(run, jobs, artifact, "arm64", RUN, COMMIT)

    def test_wrong_run_repo_commit_event_and_workflow_rejected(self):
        for key, replacement in (("id", RUN + 1), ("head_sha", "4" * 40), ("repository", {"full_name": "x/y"}),
                                 ("head_repository", {"full_name": "x/y"}), ("event", "pull_request"), ("path", ".github/workflows/other.yml")):
            run, jobs, artifact = metadata(); run[key] = replacement
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                inc.validate_remote_metadata(run, jobs, artifact, "arm64", RUN, COMMIT)

    def test_wrong_artifact_identity_and_missing_digest_rejected(self):
        for key, replacement in (("name", "native-x86_64"), ("expired", True), ("digest", ""),
                                 ("workflow_run", {"id": RUN + 1, "head_sha": COMMIT})):
            run, jobs, artifact = metadata(); artifact[key] = replacement
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                inc.validate_remote_metadata(run, jobs, artifact, "arm64", RUN, COMMIT)


class ClosureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def test_only_texture_changed_and_complete_identical_intersection_retained(self):
        old, new = source_fixture()
        closure = inc.verify_source_closure(old, new, self.root)
        self.assertEqual(closure["changed_files"], ["GILLTEXTURE/Source/PluginEditor.cpp"])
        expected = {name: digest for name, digest in old["files"].items() if new["files"].get(name) == digest}
        self.assertEqual(closure["unchanged_files"], expected)
        self.assertEqual(closure["base_unchanged_sha256"], closure["current_unchanged_sha256"])
        self.assertEqual(closure["base_unchanged_sha256"], p.digest_manifest(expected))
        self.assertIn("GILLTEXTURE/Source/TextureDSP.h", closure["unchanged_files"])

    def test_any_shared_product_juce_catalog_matrix_change_rejected(self):
        for name in ("GILLCommon/QualityBus.h", "GILLEQ/Source/Engine.h", "JUCE_PIN", "PRODUCT_CATALOG", "CTEST_MATRIX"):
            old, new = source_fixture(); new["files"][name] = "0" * 64; redigest(new)
            with self.subTest(name=name), self.assertRaises(RuntimeError): inc.verify_source_closure(old, new, self.root)

    def test_added_or_deleted_unchanged_product_file_rejected(self):
        for remove in (False, True):
            old, new = source_fixture()
            if remove: del new["files"]["GILLEQ/Source/Engine.h"]
            else: new["files"]["GILLEQ/Source/New.h"] = "0" * 64
            redigest(new)
            with self.subTest(remove=remove), self.assertRaises(RuntimeError): inc.verify_source_closure(old, new, self.root)

    def test_new_texture_file_is_explicit_in_changed_union(self):
        old, new = source_fixture(); new["files"]["GILLTEXTURE/Tests/New.cpp"] = "a" * 64; redigest(new)
        self.assertIn("GILLTEXTURE/Tests/New.cpp", inc.verify_source_closure(old, new, self.root)["changed_files"])

    def test_forged_manifest_digest_and_no_changes_rejected(self):
        old, new = source_fixture(); new["source_sha256"] = "0" * 64
        with self.assertRaises(RuntimeError): inc.verify_source_closure(old, new, self.root)
        with self.assertRaises(RuntimeError): inc.verify_source_closure(old, old, self.root)

    def test_cross_group_texture_include_rejected(self):
        old, new = source_fixture()
        path = self.root / "GILLEQ/Source/Engine.h"; path.parent.mkdir(parents=True)
        path.write_text('#include "../../GILLTEXTURE/Source/TextureDSP.h"\n')
        with self.assertRaisesRegex(RuntimeError, "references the rebuilt group"):
            inc.verify_source_closure(old, new, self.root)

    def test_frozen_recipe_bytes_are_not_normalized_or_ignored(self):
        for relative in inc.RECIPES:
            path = self.root / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"recipe\n")
        records = inc.verify_recipes(self.root, COMMIT, lambda path, commit: b"recipe\n")
        self.assertEqual(len(records), 3)
        with self.assertRaises(RuntimeError): inc.verify_recipes(self.root, COMMIT, lambda path, commit: b"recipe\r\n")


class DownloadTests(unittest.TestCase):
    def exercise(self, *, corrupt_digest=False, wrong_entry=False, unsafe_tar=False, missing_second=False):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            tar_bytes = io.BytesIO()
            with tarfile.open(fileobj=tar_bytes, mode="w:gz") as tar:
                entry = tarfile.TarInfo("../escape.txt" if unsafe_tar else "native-build.json")
                payload = b'{"fixture_only":true}'
                entry.size = len(payload); tar.addfile(entry, io.BytesIO(payload))
            zipped = io.BytesIO()
            with zipfile.ZipFile(zipped, "w") as z:
                z.writestr("other.tar.gz" if wrong_entry else "native-arm64.tar.gz", tar_bytes.getvalue())
            run, jobs, artifact = metadata()
            artifact["digest"] = "sha256:" + ("0" * 64 if corrupt_digest else inc.byte_sha(zipped.getvalue()))
            other = {"name": "native-x86_64", "expired": False}
            def response(endpoint):
                if "/jobs?" in endpoint: return {"jobs": jobs}
                if "/artifacts?" in endpoint: return {"artifacts": [artifact] + ([] if missing_second else [other])}
                return run
            def transfer(command, stdout, **kwargs): stdout.write(zipped.getvalue())
            with patch.object(inc, "gh_json", side_effect=response), patch.object(inc.subprocess, "run", side_effect=transfer):
                recovered, receipt, remote = inc.download_base(RUN, COMMIT, "arm64", root)
            self.assertTrue((recovered / "native-build.json").is_file())
            self.assertEqual(receipt["github_digest"], "sha256:" + receipt["zip_sha256"])
            self.assertEqual(receipt["native_tar_sha256"], inc.byte_sha(tar_bytes.getvalue()))
            self.assertEqual(remote["run"]["id"], RUN)

    def test_verified_zip_and_tar_are_bound_separately(self): self.exercise()
    def test_download_digest_mismatch_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "ZIP differs"): self.exercise(corrupt_digest=True)
    def test_unexpected_zip_payload_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Unexpected content"): self.exercise(wrong_entry=True)
    def test_unsafe_tar_path_rejected(self):
        with self.assertRaises((tarfile.FilterError, RuntimeError)): self.exercise(unsafe_tar=True)
    def test_second_native_artifact_required(self):
        with self.assertRaisesRegex(RuntimeError, "Both successful"): self.exercise(missing_second=True)


class BaseReportTests(unittest.TestCase):
    def test_wrong_architecture_source_and_chained_reuse_rejected_before_copy(self):
        for report in ({"passed": False}, {"passed": True, "architecture": "x86_64"},
                       {"passed": True, "architecture": "arm64", "source": {"source_sha256": "wrong"}},
                       {"passed": True, "architecture": "arm64", "incremental_reuse": {}}):
            with self.subTest(report=report), self.assertRaises(RuntimeError):
                inc.verify_base_report(report, SOURCE_SHA, "arm64", Path("unused"))

    def test_failed_native_gate_cannot_be_replaced_with_source_equality(self):
        report = {"passed": True, "architecture": "arm64", "source": {"source_sha256": SOURCE_SHA}, "plugins": []}
        with self.assertRaisesRegex(RuntimeError, "catalog differs"):
            inc.verify_base_report(report, SOURCE_SHA, "arm64", Path("unused"))

    def test_workflow_retains_all_fresh_downstream_release_gates(self):
        text = (Path(__file__).parent / "github-actions.yml").read_text(encoding="utf-8")
        self.assertIn("incremental_texture.py --source work", text)
        self.assertIn('python3 "$PIPELINE" build --source work', text)
        for gate in ('python3 "$PIPELINE" merge', 'python3 "$PIPELINE" validate',
                     'python3 "$PIPELINE" package', '--test-install'):
            self.assertIn(gate, text)
        self.assertIn("cancel-in-progress: false", text)
        self.assertIn("--base-source-sha", text)


if __name__ == "__main__":
    unittest.main()
