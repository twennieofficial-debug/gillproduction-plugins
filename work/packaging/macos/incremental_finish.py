"""Fail-closed FINISH reuse eligibility checks; this module never builds or approves releases.

The current SILK repair changes GILLCommon/LiveCausal.h and therefore requires
FULL_REBUILD on both native architectures. Metadata alone never authorizes reuse.
"""
from __future__ import annotations
import json
from pathlib import Path, PurePosixPath
import re
import pipeline as p
from incremental_texture import (validate_inputs, validate_remote_metadata, download_base,
                                 github_blob, byte_sha, REPOSITORY, NATIVE_JOBS)

REBUILD_GROUP = "GILLFINISH"
REQUIRED_FINISH_TESTS = {"EFFECTS_INTEGRATION", "Character_DSP", "Spectral_DSP"}
RECIPES = ("work/packaging/macos/pipeline.py", "work/packaging/macos/products.json")
MATRIX = "work/packaging/macos/ctest-matrix.json"


def validate_matrix_delta(old_bytes, new_bytes):
    old, new = json.loads(old_bytes), json.loads(new_bytes)
    p.require(isinstance(old, dict) and isinstance(new, dict) and set(old) == set(new) == set(p.GROUPS),
              "CTest group catalog differs")
    for matrix in (old, new):
        for tests in matrix.values():
            p.require(isinstance(tests, list) and all(isinstance(t, str) and t for t in tests)
                      and len(tests) == len(set(tests)), "Invalid CTest names")
        p.require(REQUIRED_FINISH_TESTS.issubset(matrix[REBUILD_GROUP]), "Existing FINISH tests cannot be removed")
    p.require(all(old[g] == new[g] for g in p.GROUPS if g != REBUILD_GROUP),
              "Non-FINISH test recipe changed; FULL_REBUILD required")
    p.require(set(old[REBUILD_GROUP]).issubset(new[REBUILD_GROUP]), "FINISH test removal is forbidden")
    return {"base": old, "current": new, "base_sha256": byte_sha(old_bytes),
            "current_sha256": byte_sha(new_bytes), "base_bytes_hex": old_bytes.hex(),
            "current_bytes_hex": new_bytes.hex()}


def verify_recipes(repository_root, commit, fetch_blob=github_blob):
    records = []
    for relative in RECIPES:
        old, new = fetch_blob(relative, commit), (Path(repository_root) / relative).read_bytes()
        p.require(old == new, f"Native recipe changed; FULL_REBUILD required: {relative}")
        records.append({"path": relative, "sha256": byte_sha(old)})
    matrix = validate_matrix_delta(fetch_blob(MATRIX, commit), (Path(repository_root) / MATRIX).read_bytes())
    return {"identical_recipes": records, "matrix": matrix}


def verify_source_closure(base, current, source, matrix=None):
    p.require(base.get("products") == current.get("products") == p.PRODUCT_COUNT == 74,
              "Product count differs")
    p.require(base.get("groups") == current.get("groups") == p.GROUPS, "Group catalog differs")
    old, new = base.get("files", {}), current.get("files", {})
    for report, files in ((base, old), (current, new)):
        p.require(files and p.digest_manifest(files) == report.get("source_sha256"), "Source hash mismatch")
        for name, digest in files.items():
            path = PurePosixPath(name)
            p.require(name and not path.is_absolute() and '..' not in path.parts and '\\' not in name
                      and ':' not in name and path.as_posix() == name, "Unsafe source path")
            p.require(isinstance(digest, str) and re.fullmatch(r'[0-9a-f]{40}' if name == 'JUCE_PIN' else r'[0-9a-f]{64}', digest),
                      "Invalid source digest")
    p.require(old.get("JUCE_PIN") == new.get("JUCE_PIN") == p.JUCE_COMMIT, "Pinned JUCE changed")
    changes = sorted(name for name in set(old) | set(new) if old.get(name) != new.get(name))
    product_changes = [name for name in changes if name != "CTEST_MATRIX"]
    p.require(product_changes, "No changed FINISH source; reuse is a no-op")
    p.require(all(name.startswith(REBUILD_GROUP + '/') for name in product_changes),
              "Shared or non-FINISH source changed; FULL_REBUILD required: " + ', '.join(product_changes))
    if "CTEST_MATRIX" in changes:
        p.require(matrix is not None and old.get("CTEST_MATRIX") == matrix["base_sha256"]
                  and new.get("CTEST_MATRIX") == matrix["current_sha256"], "Unbound CTest matrix change")
        # Revalidate semantic proof rather than trusting a caller-supplied flag.
        validated = validate_matrix_delta(bytes.fromhex(matrix["base_bytes_hex"]), bytes.fromhex(matrix["current_bytes_hex"]))
        p.require(validated == matrix, "CTest matrix proof does not match original bytes")
    unchanged = {name: digest for name, digest in old.items() if new.get(name) == digest}
    root = Path(source).resolve()
    for name in unchanged:
        if name in ("JUCE_PIN", "PRODUCT_CATALOG", "CTEST_MATRIX") or name.startswith(REBUILD_GROUP + '/'):
            continue
        path = (root / name).resolve()
        p.require(path.is_relative_to(root), "Source path escapes root")
        p.require(path.is_file() and p.sha(path) == unchanged[name], "Unchanged source bytes missing or differ: " + name)
        if path.suffix.lower() in (".h", ".hpp", ".c", ".cpp", ".mm", ".m", ".cmake") or path.name == "CMakeLists.txt":
            p.require(b'gillfinish' not in path.read_bytes().lower(), "Unchanged source references FINISH: " + name)
    return {"changed_files": changes, "unchanged_files": unchanged,
            "unchanged_sha256": p.digest_manifest(unchanged), "release_approved": False}


def verify_base_report(base, source_sha, arch, base_root, base_matrix):
    p.require(base.get("passed") is True and base.get("architecture") == arch and arch in NATIVE_JOBS,
              "Base native report did not pass on expected architecture")
    p.require("incremental_reuse" not in base, "Chained reuse forbidden")
    p.require(base.get("source", {}).get("source_sha256") == source_sha, "Wrong base source hash")
    products = base.get("plugins", [])
    p.require(len(products) == p.PRODUCT_COUNT and {r.get('name') for r in products} == {x['name'] for x in p.PRODUCTS},
              "Base product count or identities differ")
    root = Path(base_root).resolve()
    for product in p.PRODUCTS:
        bundle = root / 'plugins' / (product['name'] + '.vst3')
        p.require(bundle.resolve().is_relative_to(root), "Bundle path escapes payload")
        actual = p.inspect_bundle(bundle, product, [arch])
        p.require(actual == next(r for r in products if r['name'] == product['name']),
                  "Actual Mach-O, bundle bytes, identities or modes differ")
    suites = base.get('native_ctest', [])
    p.require(len(suites) == len(p.GROUPS) and {s.get('group') for s in suites} == set(p.GROUPS), "CTest group count differs")
    for suite in suites:
        tests = suite.get('tests', [])
        p.require(suite.get('passed') is True and len(tests) == len(set(tests))
                  and set(base_matrix[suite['group']]).issubset(tests), "Base CTest failed or coverage differs")
    p.verify_native_quality_gate(base, root)
    p.verify_native_mix_gate(base, root)


if __name__ == '__main__':
    raise SystemExit('FULL_REBUILD required for the shared SILK correction. Use the ordinary native workflow; this is a safety-check library, not a native build command.')
