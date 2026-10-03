#!/usr/bin/env python3
"""Rebuild TEXTURE only after proving that the other 57 native inputs are identical.

This is a new candidate, never an approval or relabeling of the base release.
The base run may be cancelled after its two successful native jobs upload their
artifacts. Fresh real-host, Universal strict-validation and installer gates are
still required by the ordinary workflow. Only run on a native GitHub Mac runner.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import zipfile

import pipeline as p

REPOSITORY = "twennieofficial-debug/gillproduction-plugins"
REBUILD_GROUP = "GILLTEXTURE"
RECIPES = ("work/packaging/macos/pipeline.py", "work/packaging/macos/products.json",
           "work/packaging/macos/ctest-matrix.json")
NATIVE_JOBS = {"arm64": "native-build (macos-15, arm64)",
               "x86_64": "native-build (macos-15-intel, x86_64)"}


def byte_sha(data):
    return hashlib.sha256(data).hexdigest()


def validate_inputs(repository, run_id, commit, source_sha):
    p.require(repository == REPOSITORY, "Incremental reuse is restricted to the authorized repository")
    p.require(isinstance(run_id, int) and run_id > 0, "A positive base run ID is required")
    p.require(bool(re.fullmatch(r"[0-9a-f]{40}", commit)), "A full lowercase base commit SHA is required")
    p.require(bool(re.fullmatch(r"[0-9a-f]{64}", source_sha)), "A complete base source SHA256 is required")


def gh_json(endpoint):
    return json.loads(subprocess.check_output(["gh", "api", endpoint], timeout=120))


def validate_remote_metadata(run, jobs, artifact, arch, run_id, commit):
    p.require(arch in NATIVE_JOBS, "Unsupported native architecture")
    p.require(run.get("id") == run_id and run.get("head_sha") == commit,
              "Base run/commit identity differs")
    p.require(run.get("repository", {}).get("full_name") == REPOSITORY,
              "Base run belongs to another repository")
    p.require(run.get("head_repository", {}).get("full_name") == REPOSITORY,
              "Base code comes from another repository")
    p.require(run.get("event") == "workflow_dispatch" and
              run.get("path", "").split("@", 1)[0] == ".github/workflows/build-macos.yml",
              "Base run is not the expected manually dispatched Mac workflow")
    # Overall workflow success is deliberately NOT required: its old UI is not
    # release-approved. Both successful native jobs are the reusable evidence.
    for architecture, name in NATIVE_JOBS.items():
        matching = [job for job in jobs if job.get("name") == name]
        p.require(len(matching) == 1 and matching[0].get("status") == "completed" and
                  matching[0].get("conclusion") == "success",
                  f"Base {architecture} native job has not completed successfully")
    p.require(artifact.get("name") == f"native-{arch}" and artifact.get("expired") is False,
              "Base artifact name, architecture or expiry differs")
    identity = artifact.get("workflow_run", {})
    p.require(identity.get("id") == run_id and identity.get("head_sha") == commit,
              "Base artifact belongs to a different run/commit")
    p.require(bool(re.fullmatch(r"sha256:[0-9a-f]{64}", artifact.get("digest", ""))),
              "GitHub artifact SHA256 digest is missing")


def verify_source_closure(base, current, source):
    p.require(base.get("products") == current.get("products") == 60,
              "The incremental candidate must contain exactly 60 products")
    p.require(base.get("groups") == current.get("groups") == p.GROUPS,
              "Source group catalog changed")
    old, new = base.get("files", {}), current.get("files", {})
    p.require(old and new and p.digest_manifest(old) == base.get("source_sha256") and
              p.digest_manifest(new) == current.get("source_sha256"), "Source manifest digest is invalid")
    changed = sorted(name for name in set(old) | set(new) if old.get(name) != new.get(name))
    p.require(changed and all(name.startswith(REBUILD_GROUP + "/") for name in changed),
              "Reuse permits source changes only inside GILLTEXTURE")
    # Include every identical input, even unchanged files within TEXTURE, so a
    # verifier can reproduce this receipt directly from the complete manifests.
    old_closure = {name: digest for name, digest in old.items() if new.get(name) == digest}
    new_closure = {name: digest for name, digest in new.items() if old.get(name) == digest}
    p.require(old_closure == new_closure, "An unchanged product's complete source closure differs")
    p.require(old.get("JUCE_PIN") == new.get("JUCE_PIN") == p.JUCE_COMMIT, "Pinned JUCE changed")
    # The unchanged closure includes ALL other product sources, third parties,
    # shared UI/DSP/host sources, assets, CMake files, catalog and test matrix.
    # Refuse a reverse dependency on the one changed group, including CMake.
    for name in new_closure:
        if name.startswith(REBUILD_GROUP + "/"):
            continue
        path = Path(source) / name
        if path.is_file() and (path.suffix.lower() in (".h", ".hpp", ".c", ".cpp", ".mm", ".m", ".cmake")
                               or path.name == "CMakeLists.txt"):
            p.require(b"gilltexture" not in path.read_bytes().lower(),
                      f"An unchanged source references the rebuilt group: {name}")
    return {"base_unchanged_sha256": p.digest_manifest(old_closure), "current_unchanged_sha256": p.digest_manifest(new_closure),
            "changed_files": changed, "unchanged_files": new_closure,
            "juce_commit": p.JUCE_COMMIT, "cross_group_references_checked": True}


def verify_recipes(repository_root, commit, fetch_blob):
    records = []
    for relative in RECIPES:
        old = fetch_blob(relative, commit)
        current = (Path(repository_root) / relative).read_bytes()
        p.require(old == current, f"Frozen native build/host/merge recipe changed: {relative}")
        records.append({"path": relative, "base_sha256": byte_sha(old), "current_sha256": byte_sha(current)})
    return records


def github_blob(relative, commit):
    import base64
    item = gh_json(f"repos/{REPOSITORY}/contents/{relative}?ref={commit}")
    p.require(item.get("type") == "file" and item.get("encoding") == "base64",
              f"Cannot read immutable base recipe: {relative}")
    data = base64.b64decode(item["content"], validate=False)
    p.require(len(data) == item.get("size"), "Base recipe byte count differs")
    return data


def download_base(run_id, commit, arch, root):
    api = f"repos/{REPOSITORY}/actions/runs/{run_id}"
    run = gh_json(api)
    jobs = gh_json(api + "/jobs?per_page=100")["jobs"]
    artifacts = gh_json(api + "/artifacts?per_page=100")["artifacts"]
    matching = [item for item in artifacts if item.get("name") == f"native-{arch}" and not item.get("expired")]
    p.require(len(matching) == 1, "Expected one current native base artifact")
    artifact = matching[0]
    validate_remote_metadata(run, jobs, artifact, arch, run_id, commit)
    for architecture in NATIVE_JOBS:
        p.require(len([a for a in artifacts if a.get("name") == f"native-{architecture}" and not a.get("expired")]) == 1,
                  "Both successful base native artifacts must be available")
    download = p.new_destination(Path(root) / "base-download")
    archive = download / f"native-{arch}.zip"
    with archive.open("wb") as stream:
        subprocess.run(["gh", "api", f"repos/{REPOSITORY}/actions/artifacts/{artifact['id']}/zip"],
                       stdout=stream, check=True, timeout=1200)
    zip_sha = p.sha(archive)
    p.require("sha256:" + zip_sha == artifact["digest"], "Downloaded ZIP differs from GitHub's artifact digest")
    tar_path = download / f"native-{arch}.tar.gz"
    with zipfile.ZipFile(archive) as zipped:
        entries = [member for member in zipped.infolist() if not member.is_dir()]
        p.require(len(entries) == 1 and entries[0].filename == tar_path.name,
                  "Unexpected content in the native artifact ZIP")
        p.require(entries[0].file_size <= 4 * 1024**3, "Native archive exceeds size bound")
        with zipped.open(entries[0]) as incoming, tar_path.open("wb") as outgoing:
            shutil.copyfileobj(incoming, outgoing)
    recovered = p.new_destination(Path(root) / "base-native")
    with tarfile.open(tar_path, "r:gz") as packed:
        members = packed.getmembers()
        p.require(sum(member.size for member in members) <= 6 * 1024**3, "Native payload exceeds size bound")
        packed.extractall(recovered, members=members, filter="data")
    receipt = {"id": artifact["id"], "name": artifact["name"], "github_digest": artifact["digest"],
               "zip_sha256": zip_sha, "native_tar_sha256": p.sha(tar_path)}
    return recovered, receipt, {"run": run, "native_jobs": [job for job in jobs if job.get("name") in NATIVE_JOBS.values()],
                                "artifact": artifact}


def verify_base_report(base, source_sha, arch, base_root):
    p.require(base.get("passed") is True and base.get("architecture") == arch,
              "The base native report did not pass on this architecture")
    p.require("incremental_reuse" not in base, "Chained incremental reuse is not supported")
    p.require(base.get("source", {}).get("source_sha256") == source_sha, "Unexpected base source fingerprint")
    expected = {product["name"]: product for product in p.PRODUCTS}
    records = base.get("plugins", [])
    p.require(len(records) == 60 and {item.get("name") for item in records} == set(expected),
              "Base native bundle catalog differs")
    for record in records:
        product = expected[record["name"]]
        p.require(record.get("version") == product["version"] == p.SUITE_VERSION and
                  record.get("bundle_id") == product["bundle_id"], "Base product version or identity changed")
        bundle = Path(base_root) / "plugins" / (product["name"] + ".vst3")
        p.require(p.bundle_digest(bundle) == record.get("bundle_sha256"), "Base bundle bytes or modes changed")
    suites = base.get("native_ctest", [])
    p.require(len(suites) == len(p.GROUPS) and {s.get("group") for s in suites} == set(p.GROUPS),
              "Base native CTest group coverage differs")
    for suite in suites:
        p.require(suite.get("passed") is True, "A base native CTest group failed")
        p.verify_ctest_listing(suite["group"], {"tests": [{"name": name} for name in suite.get("tests", [])]})
    p.verify_native_quality_gate(base, base_root)
    p.verify_native_mix_gate(base, base_root)


def collect_ui(dest):
    ui = []
    for product in p.PRODUCTS:
        matches = []
        for path in (Path(dest) / "test-evidence" / product["group"]).glob(f'{product["name"]}-UI-*.png'):
            with path.open("rb") as stream:
                header = stream.read(24)
            if len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and list(struct.unpack(">II", header[16:24])) == product["default_size"]:
                matches.append(path.relative_to(dest).as_posix())
        p.require(matches, f"Missing compact-size Mac screenshot: {product['name']}")
        ui.append({"name": product["name"], "size": product["default_size"], "screenshots": sorted(matches)})
    return ui


def reuse_unchanged(base, base_root, dest, arch, run_id, source_sha):
    reused_groups = [group for group in p.GROUPS if group != REBUILD_GROUP]
    suites = []
    for group in reused_groups:
        original = next(item for item in base["native_ctest"] if item["group"] == group)
        suite = copy.deepcopy(original)
        suite.update(execution="reused", base_run_id=run_id, base_source_sha256=source_sha)
        suites.append(suite)
        incoming = Path(base_root) / "test-evidence" / group
        p.require(incoming.is_dir(), f"Missing original group evidence: {group}")
        shutil.copytree(incoming, Path(dest) / "test-evidence" / group, symlinks=True)
        for path in (Path(base_root) / "logs").glob(group + "-*"):
            if path.is_file():
                target = Path(dest) / "logs" / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    products = []
    for product in p.PRODUCTS:
        if product["group"] == REBUILD_GROUP:
            continue
        name = product["name"]
        old = next(item for item in base["plugins"] if item["name"] == name)
        incoming = Path(base_root) / "plugins" / (name + ".vst3")
        outgoing = Path(dest) / "plugins" / incoming.name
        shutil.copytree(incoming, outgoing, symlinks=True)
        current = p.inspect_bundle(outgoing, product, [arch])
        p.require(current == old, f"Reused native module or bundle changed: {name}")
        ui = next((item for item in base.get("compact_ui_dimensions", []) if item.get("name") == name), None)
        p.require(ui and ui.get("size") == product["default_size"] and ui.get("screenshots"),
                  f"Base compact UI record is missing: {name}")
        images = []
        for relative in ui["screenshots"]:
            before, after = (Path(base_root) / relative).resolve(), (Path(dest) / relative).resolve()
            p.require(before.is_relative_to(Path(base_root).resolve()) and after.is_relative_to(Path(dest).resolve()),
                      "UI evidence path escapes its native payload")
            p.require(before.is_file() and after.is_file() and p.sha(before) == p.sha(after),
                      f"Reused UI image bytes differ: {name}")
            images.append({"path": relative, "sha256": p.sha(after)})
        products.append({"name": name, "group": product["group"], "base_bundle_sha256": old["bundle_sha256"],
                         "current_bundle_sha256": current["bundle_sha256"], "mach_o": current["mach_o"],
                         "screenshots": images})
    p.require(len(products) == 57, "Exactly 57 unchanged native products must be reused")
    return suites, products


def build(args):
    arch = p.native_arch()
    validate_inputs(os.environ.get("GITHUB_REPOSITORY", ""), args.base_run, args.base_commit, args.base_source_sha)
    source, root = Path(args.source).resolve(), Path(args.build_root).resolve()
    snapshot = p.source_check(source)
    recipes = verify_recipes(source.parent, args.base_commit, github_blob)
    base_root, artifact, remote = download_base(args.base_run, args.base_commit, arch, root)
    original = base_root / "native-build.json"
    base = p.read(original)
    verify_base_report(base, args.base_source_sha, arch, base_root)
    closure = verify_source_closure(base["source"], snapshot, source)
    closure["recipes"] = recipes
    dest = p.new_destination(args.destination)
    provenance = dest / "test-evidence/REUSE"
    provenance.mkdir(parents=True)
    shutil.copy2(original, provenance / "base-native-build.json")
    p.write(provenance / "github-base-metadata.json", remote)
    p.write(provenance / "source-closure.json", closure)
    suites, reused = reuse_unchanged(base, base_root, dest, arch, args.base_run, args.base_source_sha)
    receipt = {"schema": 1, "repository": REPOSITORY, "base_run_id": args.base_run,
               "base_commit": args.base_commit, "base_source_sha256": args.base_source_sha,
               "base_artifact": artifact,
               "base_native_report": {"path": "test-evidence/REUSE/base-native-build.json", "sha256": p.sha(original)},
               "source_closure": closure, "rebuilt_groups": [REBUILD_GROUP],
               "reused_groups": [g for g in p.GROUPS if g != REBUILD_GROUP], "reused_products": reused,
               "fresh_host_gates": ["QualityHost", "MixRealHost"],
               "base_release_approved": False}
    try:
        fresh = p.build_group(REBUILD_GROUP, source, root, dest, arch, time.time_ns(), args.jobs)
        fresh["execution"] = "fresh"
        suites.append(fresh)
        suites.sort(key=lambda item: item["group"])
        p.require(p.source_check(source)["source_sha256"] == snapshot["source_sha256"], "Source changed during TEXTURE build")
        ui = collect_ui(dest)
        records = [p.inspect_bundle(dest / "plugins" / (item["name"] + ".vst3"), item, [arch]) for item in p.PRODUCTS]
        quality = p.build_quality_host(source, root, dest, arch, args.jobs, records, snapshot["source_sha256"])
        mix = p.build_mix_host(source, root, dest, arch, args.jobs, records, snapshot["source_sha256"])
        p.require(p.source_check(source)["source_sha256"] == snapshot["source_sha256"], "Source changed during fresh host gates")
        report = {"passed": True, "architecture": arch, "source": snapshot, "native_ctest": suites,
                  "compact_ui_dimensions": ui, "plugins": records, "quality_host": quality, "mix_host": mix,
                  "incremental_reuse": receipt, "fl_studio_tested": False, "visual_review_required": True}
        p.verify_native_quality_gate(report, dest)
        p.verify_native_mix_gate(report, dest)
        p.write(dest / "native-build.json", report)
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        p.write(dest / "native-failure.json", {"passed": False, "architecture": arch, "source": snapshot,
                "incremental_reuse": receipt, "completed_groups": suites, "error": str(error)})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--destination", required=True)
    parser.add_argument("--base-run", type=int, required=True)
    parser.add_argument("--base-commit", required=True)
    parser.add_argument("--base-source-sha", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    build(parser.parse_args())


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        print(f"STOP: {error}", file=sys.stderr)
        sys.exit(1)
