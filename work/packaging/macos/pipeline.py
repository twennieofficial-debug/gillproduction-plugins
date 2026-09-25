#!/usr/bin/env python3
"""GILL macOS build, native verification, Universal merge, and Installer/DMG.

This script never turns a Windows DLL into a Mac plug-in and never disables
Gatekeeper. Packaging requires successful native checks of the exact Universal
bundle bytes on both architectures. Run --help for the independently resumable
phases. All command arguments are passed without a shell.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
PRODUCTS = json.loads((HERE / "products.json").read_text(encoding="utf-8"))
CTEST_MATRIX = json.loads((HERE / "ctest-matrix.json").read_text(encoding="utf-8"))
GROUPS = sorted({p["group"] for p in PRODUCTS})
PRODUCT_COUNT = len(PRODUCTS)
JUCE_COMMIT = "29396c22c93392d6738e021b83196283d6e4d850"
MIN_MACOS = "11.0"
RELEASE = "06"
SUITE_VERSION = "0.6.0"
QUALITY_SAMPLE_RATES = (44100, 48000, 96000, 192000)
PLUGINVAL_URL = "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip"
PLUGINVAL_SHA256 = "3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f"
MACHO_MAGICS = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
                b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
                b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def ctest_failure_excerpt(text):
    """Bounded first/last failure context plus final CTest summary for live CI."""
    lines = text.splitlines()
    if not lines:
        return "CTest produced no output; inspect the retained full log."
    failure = re.compile(r"(?:^|\s)(?:FAIL(?:ED)?\b|error\b|assert(?:ion)?\b)|\*\*\*", re.IGNORECASE)
    hits = [i for i, line in enumerate(lines) if failure.search(line)]
    chosen = set(range(max(0, len(lines) - 35), len(lines)))
    for index in hits[:10] + hits[-10:]:
        chosen.update(range(max(0, index - 1), min(len(lines), index + 2)))
    selected = sorted(chosen)
    excerpts = [f"{index + 1}: {lines[index][:400]}" for index in selected]
    result = "\n".join(excerpts)
    if len(result) > 18000:
        result = result[:10000] + "\n[excerpt shortened; full log retained]\n" + result[-7500:]
    return result


def run(command, *, log=None, timeout=3600):
    command = [str(x) for x in command]
    # Signing credentials are kept in the keychain, never in printed commands.
    print("RUN", command[0], " ".join(command[1:3]), flush=True)
    if log:
        Path(log).parent.mkdir(parents=True, exist_ok=True)
        with Path(log).open("wb") as f:
            process = subprocess.run(command, stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
        if process.returncode != 0 and Path(command[0]).name.lower() in ("ctest", "ctest.exe"):
            output = Path(log).read_text(encoding="utf-8", errors="replace")
            print(f"\nCTEST FAILURE DETAILS — {Path(log).name}\n{ctest_failure_excerpt(output)}\nEND CTEST FAILURE DETAILS\n",
                  flush=True)
        require(process.returncode == 0, f"Command failed ({process.returncode}); see {log}")
        return Path(log).read_text(encoding="utf-8", errors="replace")
    return subprocess.check_output(command, stderr=subprocess.STDOUT, text=True, timeout=timeout).strip()


def native_arch():
    require(sys.platform == "darwin", "This phase requires a real macOS machine with Xcode tools.")
    arch = platform.machine()
    require(arch in ("arm64", "x86_64"), f"Unsupported native architecture: {arch}")
    # Refuse Rosetta as a substitute for a native Intel validation job.
    translated = subprocess.run(["sysctl", "-n", "sysctl.proc_translated"], capture_output=True, text=True)
    require(translated.stdout.strip() != "1", "Run this phase natively, not through Rosetta.")
    return arch


def tree_manifest(root):
    root = Path(root).resolve()
    result = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            require(path.resolve().is_relative_to(root), f"Bundle symlink escapes: {relative}")
            result[relative] = {"symlink": os.readlink(path)}
        elif path.is_file():
            result[relative] = {"sha256": sha(path), "executable": bool(path.stat().st_mode & 0o111)}
    return result


def digest_manifest(manifest):
    return hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def bundle_digest(root):
    return digest_manifest(tree_manifest(root))


def source_check(source):
    source = Path(source).resolve()
    require(PRODUCT_COUNT > 0 and len({p["code"] for p in PRODUCTS}) == PRODUCT_COUNT, "Unique plug-in codes required")
    require(len({p["bundle_id"] for p in PRODUCTS}) == PRODUCT_COUNT, "Duplicate bundle identifiers")
    require(len({p["target"] for p in PRODUCTS}) == PRODUCT_COUNT and len({p["name"] for p in PRODUCTS}) == PRODUCT_COUNT, "Duplicate product names or targets")
    require(set(GROUPS) == set(CTEST_MATRIX), "Each catalog group requires a native test gate")
    selected = {}
    common = source / "GILLCommon"
    for required in ("QualityBus.h", "QualityUi.h", "MaterialUi.h", "Tests/QualityHost.cpp",
                     "Tests/MacQualityHost.mm", "Tests/QualityHostCMake/CMakeLists.txt"):
        require((common / required).is_file(), f"Missing shared source: GILLCommon/{required}")
    for path in sorted(common.rglob("*")):
        if path.is_file() and path.suffix.lower() in (".h", ".hpp", ".cpp", ".c", ".mm", ".md", ".txt"):
            selected[path.relative_to(source).as_posix()] = sha(path)
    for group in GROUPS:
        root = source / group
        require((root / "CMakeLists.txt").is_file(), f"Missing source group: {group}")
        for folder in (root / "Source", root / "Assets", root / "ThirdParty"):
            if folder.exists():
                for path in sorted(folder.rglob("*")):
                    if path.is_file():
                        selected[path.relative_to(source).as_posix()] = sha(path)
        selected[f"{group}/CMakeLists.txt"] = sha(root / "CMakeLists.txt")
        for path in sorted((root / "Tests").rglob("*")):
            parts = path.relative_to(root / "Tests").parts
            if any(part in {"__pycache__", "_python", ".git", "auto-quality", "final-ui-review"} or part.startswith(("build", "pluginval")) or part.endswith("_artefacts") for part in parts):
                continue
            code = path.suffix.lower() in (".cpp", ".h", ".hpp", ".c", ".mm", ".m")
            # Match prepare_repository.py's licensed *input* fixture selection.
            # Tests generate processed audio and nested latency baselines, which
            # must not change the source fingerprint after native CTest runs.
            fixture = (group in {"GILLDEREVERB", "GILLRESTORATION"} and path.parent == root / "Tests/fixtures" and
                       (path.suffix.lower() in (".wav", ".flac") or path.name in ("ORIGIN-AND-LICENSE.md", "provenance.json", "prepare_fixtures.py")) and
                       not path.name.startswith(("processed_", "DECLICK-clean", "DECLICK-mouth-restored", "DECLICK-restored",
                                                 "DECRACKLE-clean", "DECRACKLE-mouth-restored", "DECRACKLE-restored")))
            if path.is_file() and (code or fixture):
                selected[path.relative_to(source).as_posix()] = sha(path)
    juce = source / "dependencies" / "JUCE"
    require((juce / "CMakeLists.txt").is_file(), "Pinned JUCE source is missing")
    if (juce / ".git").exists():
        require(run(["git", "-C", juce, "rev-parse", "HEAD"]) == JUCE_COMMIT, "JUCE commit differs")
        require(not run(["git", "-C", juce, "status", "--porcelain"]), "JUCE source has uncommitted changes")
    selected["JUCE_PIN"] = JUCE_COMMIT
    selected["PRODUCT_CATALOG"] = sha(HERE / "products.json")
    selected["CTEST_MATRIX"] = sha(HERE / "ctest-matrix.json")
    return {"products": PRODUCT_COUNT, "groups": GROUPS, "source_sha256": digest_manifest(selected), "files": selected}


def macho(path):
    with Path(path).open("rb") as f:
        return f.read(4) in MACHO_MAGICS


def inspect_bundle(bundle, product, architectures):
    bundle = Path(bundle)
    info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
    require(info.get("CFBundleIdentifier") == product["bundle_id"], f"Bundle identifier changed: {bundle.name}")
    require(info.get("CFBundleShortVersionString") == product["version"], f"Bundle version changed: {bundle.name}")
    require(info.get("CFBundleVersion") == product["version"], f"Bundle build version changed: {bundle.name}")
    exe = bundle / "Contents/MacOS" / info["CFBundleExecutable"]
    require(exe.is_file() and macho(exe), f"Not a Mach-O plug-in: {exe}")
    records = []
    for path in sorted(bundle.rglob("*")):
        if not path.is_file() or path.is_symlink() or not macho(path):
            continue
        archs = set(run(["lipo", "-archs", path]).split())
        require(archs == set(architectures), f"Wrong architectures in {path}: {archs}")
        for arch in sorted(archs):
            linked = run(["otool", "-arch", arch, "-L", path]).splitlines()[1:]
            dependencies = [line.strip().split(" (", 1)[0] for line in linked if line.strip()]
            # This bundle statically links JUCE/Signalsmith. No Homebrew, build
            # directory, developer machine, or unresolved @rpath dependencies.
            require(all(d.startswith(("/System/Library/", "/usr/lib/")) for d in dependencies),
                    f"Non-system runtime dependency: {path}: {dependencies}")
            load_commands = run(["otool", "-arch", arch, "-l", path])
            versions = []
            lines = load_commands.splitlines()
            for index, line in enumerate(lines):
                if line.strip() in ("cmd LC_BUILD_VERSION", "cmd LC_VERSION_MIN_MACOSX"):
                    for candidate in lines[index + 1:index + 7]:
                        bits = candidate.strip().split()
                        if bits and bits[0] in ("minos", "version") and len(bits) == 2:
                            versions.append(bits[1])
                            break
            require(versions, f"No deployment target found: {path}")
            require(all(tuple(map(int, v.split("."))) <= (11, 0, 0) for v in versions),
                    f"Deployment target exceeds macOS 11: {path}: {versions}")
        records.append({"path": path.relative_to(bundle).as_posix(), "architectures": sorted(archs), "sha256": sha(path)})
    require(records, f"No executable in {bundle}")
    return {"name": product["name"], "version": product["version"], "bundle_id": product["bundle_id"],
            "bundle_sha256": bundle_digest(bundle), "mach_o": records}


def new_destination(path):
    path = Path(path).resolve()
    require(not path.exists() or not any(path.iterdir()), f"Destination must be new/empty: {path}")
    path.mkdir(parents=True, exist_ok=True)
    return path


def verify_ctest_listing(group, listing):
    discovered = [test["name"] for test in listing.get("tests", [])]
    expected = CTEST_MATRIX[group]
    require(len(discovered) == len(set(discovered)) and set(expected).issubset(discovered),
            f"Native CTest suite differs for {group}; missing={sorted(set(expected) - set(discovered))}, "
            f"duplicate_entries={len(discovered) - len(set(discovered))}")


def collect_test_evidence(source_group, build_dir, dest, since_ns):
    """Collect only fresh results, including those left by a failed CTest run."""
    source_group = Path(source_group)
    candidates = list((source_group / "Tests").glob("*"))
    # New groups keep generated evidence in their build directory. Include only
    # result-shaped files there, not CMake caches or unrelated build inputs.
    candidates += list(Path(build_dir).glob("*-UI-*.png"))
    candidates += list(Path(build_dir).glob("*-report.json"))
    for path in candidates:
        if path.is_file() and path.stat().st_mtime_ns >= since_ns and path.suffix in (".png", ".json", ".txt", ".csv"):
            target = Path(dest) / "test-evidence" / source_group.name / path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    for name in ("LastTest.log", "LastTestsFailed.log"):
        path = Path(build_dir) / "Testing/Temporary" / name
        if path.is_file() and path.stat().st_mtime_ns >= since_ns:
            target = Path(dest) / "logs" / f"{source_group.name}-{name}"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def build_group(group, source, root, dest, arch, start, jobs):
    build_dir = root / group
    command = ["cmake", "-S", source / group, "-B", build_dir, "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_OSX_ARCHITECTURES={arch}",
               f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MIN_MACOS}", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"]
    run(command, log=dest / "logs" / f"{group}-configure.txt")
    run(["cmake", "--build", build_dir, "--config", "Release", "--parallel", jobs],
        log=dest / "logs" / f"{group}-build.txt", timeout=7200)
    listing = json.loads(run(["ctest", "--test-dir", build_dir, "-C", "Release", "--show-only=json-v1"]))
    verify_ctest_listing(group, listing)
    try:
        run(["ctest", "--test-dir", build_dir, "-C", "Release", "--output-on-failure", "--timeout", "1800", "-j", "1"],
            log=dest / "logs" / f"{group}-ctest.txt", timeout=7200)
    except Exception:
        # Preserve the CTest error even if collecting diagnostic files also
        # fails; copying screenshots must never turn a failed test green.
        try:
            collect_test_evidence(source / group, build_dir, dest, start)
        except Exception as evidence_error:
            print(f"Could not collect all failure evidence: {evidence_error}", file=sys.stderr)
        raise
    collect_test_evidence(source / group, build_dir, dest, start)
    for product in (p for p in PRODUCTS if p["group"] == group):
        name = product["name"] + ".vst3"
        bundle = build_dir / f'{product["target"]}_artefacts/Release/VST3' / name
        inspect_bundle(bundle, product, [arch])
        shutil.copytree(bundle, dest / "plugins" / name, symlinks=True)
    return {"group": group, "tests": [t["name"] for t in listing["tests"]], "passed": True}


def verify_quality_result(result, rate, bundle_paths=None):
    """Validate actual GillQualityHost output; this never runs a substitute DSP."""
    require(result.get("passed") is True and result.get("failures") == 0 and result.get("checks", 0) > 0,
            f"QualityHost failed at {rate} Hz")
    require(result.get("sample_rate") == rate and result.get("actual_vst3_bundles") == PRODUCT_COUNT,
            f"QualityHost sample rate or product count differs at {rate} Hz")
    products = result.get("products", [])
    expected = {p["name"]: p for p in PRODUCTS}
    require(len(products) == PRODUCT_COUNT and {p.get("name") for p in products} == set(expected),
            f"QualityHost product list differs at {rate} Hz")
    identities = set()
    for product in products:
        name = product["name"]
        require(product.get("factory_version") == expected[name]["version"] and product.get("factory_version_verified") is True,
                f"QualityHost factory version not verified: {name}")
        identity = product.get("factory_uid", "")
        require(isinstance(identity, str) and re.fullmatch(r"[0-9a-fA-F]{1,8}", identity) and identity not in identities,
                f"QualityHost factory identity invalid or duplicated: {name}")
        identities.add(identity)
        require(product.get("manufacturer") == "GILLPRODUCTION", f"QualityHost manufacturer differs: {name}")
        if bundle_paths is not None:
            require(Path(product.get("bundle", "")).resolve() == bundle_paths[name].resolve(),
                    f"QualityHost loaded a different bundle: {name}")
        live, pro = product.get("live_latency_samples"), product.get("pro_latency_samples")
        require(type(live) is int and type(pro) is int and 0 <= live <= pro < rate,
                f"QualityHost latency evidence invalid: {name}")
        if name == "GILLCONTROL":
            require(pro == 0, "QualityHost controller must have zero latency in both modes")
        if name in ("GILLTUNE", "GILLTUNE LIVE"):
            require(live == math.ceil(rate * .016), f"QualityHost Tune LIVE latency differs: {name}")
        elif name == "GILLFORM":
            require(0 < live < rate * .025, "QualityHost Form LIVE window exceeds 25 ms")
        else:
            require(live == 0, f"QualityHost LIVE is not zero latency: {name}")


def verify_native_quality_gate(report, evidence_root=None):
    gate = report.get("quality_host", {})
    require(gate.get("passed") is True and gate.get("architecture") == report.get("architecture") and
            gate.get("source_sha256") == report.get("source", {}).get("source_sha256"),
            "Missing native QualityHost gate for this architecture/source")
    require(gate.get("executable_architectures") == [report["architecture"]] and
            re.fullmatch(r"[0-9a-f]{64}", gate.get("executable_sha256", "")),
            "QualityHost native executable identity missing")
    runs = gate.get("runs", [])
    require(len(runs) == len(QUALITY_SAMPLE_RATES) and
            sorted(r.get("sample_rate", 0) for r in runs) == list(QUALITY_SAMPLE_RATES),
            "QualityHost requires all four native sample rates")
    expected = {p["name"]: p["bundle_sha256"] for p in report.get("plugins", [])}
    require(len(expected) == PRODUCT_COUNT and gate.get("bundle_sha256") == expected,
            "QualityHost evidence refers to different native bundles")
    for run_record in runs:
        require(run_record.get("exit_code") == 0, "QualityHost execution did not succeed")
        verify_quality_result(run_record.get("result", {}), run_record["sample_rate"])
        if evidence_root is not None:
            root = Path(evidence_root).resolve()
            path = (root / run_record.get("report", "")).resolve()
            require(path.is_relative_to(root) and path.is_file(), "QualityHost report file is missing")
            require(sha(path) == run_record.get("report_sha256") and read(path) == run_record["result"],
                    "QualityHost report bytes changed")


def build_quality_host(source, root, dest, arch, jobs, records, source_sha256):
    """Build a real JUCE VST3 host and load all copied native bundles together."""
    started = time.monotonic()
    build_dir = root / "QualityHost"
    evidence = dest / "test-evidence" / "QUALITY_HOST"
    evidence.mkdir(parents=True, exist_ok=True)
    # The existing exported/prebuilt runtime is Windows-only. Native Mac builds
    # use pinned JUCE sources; ccache can reuse matching compilation units.
    run(["cmake", "-S", source / "GILLCommon/Tests/QualityHostCMake", "-B", build_dir, "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_OSX_ARCHITECTURES={arch}",
         f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MIN_MACOS}", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
         "-DGILL_PREBUILT_RUNTIME=", "-DGILL_JUCE_EXPORT="], log=dest / "logs/QualityHost-configure.txt")
    run(["cmake", "--build", build_dir, "--config", "Release", "--parallel", jobs],
        log=dest / "logs/QualityHost-build.txt", timeout=3600)
    executable = build_dir / "GillQualityHost"
    require(executable.is_file() and macho(executable), "QualityHost must be a real native Mach-O executable")
    architectures = run(["lipo", "-archs", executable]).split()
    require(architectures == [arch], "QualityHost executable is not the native runner architecture")
    bundles = {p["name"]: dest / "plugins" / (p["name"] + ".vst3") for p in PRODUCTS}
    before = {p["name"]: p["bundle_sha256"] for p in records}
    require(all(bundle_digest(path) == before[name] for name, path in bundles.items()),
            "Native bundles changed before QualityHost")
    paths = evidence / "bundle-paths.json"
    write(paths, {"plugins": [{"name": name, "path": str(path)} for name, path in bundles.items()]})
    results, failures = [], []
    executable_sha = sha(executable)
    for rate in QUALITY_SAMPLE_RATES:
        report = evidence / f"quality-host-{rate}.json"
        try:
            require(not report.exists(), "QualityHost requires a fresh report path")
            run([executable, "--list", paths, "--sample-rate", rate, "--report", report],
                log=dest / "logs" / f"QualityHost-{rate}.txt", timeout=600)
            result = read(report)
            verify_quality_result(result, rate, bundles)
            require(sha(executable) == executable_sha, "QualityHost executable changed during tests")
            require(all(bundle_digest(path) == before[name] for name, path in bundles.items()),
                    "Native bundles changed during QualityHost")
            results.append({"sample_rate": rate, "exit_code": 0, "result": result,
                            "report": report.relative_to(dest).as_posix(), "report_sha256": sha(report)})
        except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
            failures.append({"sample_rate": rate, "passed": False, "error": str(error)})
            print(f"FAILED QualityHost {rate} Hz: {error}", file=sys.stderr, flush=True)
    gate = {"passed": not failures, "architecture": arch, "source_sha256": source_sha256,
            "executable_architectures": architectures, "executable_sha256": executable_sha,
            "bundle_sha256": before, "runs": results, "failures": failures,
            "elapsed_seconds": round(time.monotonic() - started, 3)}
    write(evidence / "quality-host-gate.json", gate)
    require(not failures, f"{len(failures)} native QualityHost sample-rate runs failed")
    return gate


def build(args):
    arch = native_arch()
    source = Path(args.source).resolve()
    snapshot = source_check(source)
    dest = new_destination(args.destination)
    root = Path(args.build_root).resolve()
    start = time.time_ns()
    suites, failures = [], []
    for group in GROUPS:
        try:
            suites.append(build_group(group, source, root, dest, arch, start, args.jobs))
        except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
            failures.append({"group": group, "error": str(error), "passed": False})
            print(f"FAILED {group}; continuing independent groups: {error}", file=sys.stderr, flush=True)
    if failures:
        write(dest / "native-failure.json", {"passed": False, "architecture": arch,
              "source": snapshot, "completed_groups": suites, "failed_groups": failures})
        # No native-build.json is produced, so no merge/package phase can accept
        # a partial result. Independent groups still provide useful diagnostics.
        raise RuntimeError(f"{len(failures)} native source groups failed; see native-failure.json")
    require(source_check(source)["source_sha256"] == snapshot["source_sha256"], "Source changed during build")
    ui = []
    for product in PRODUCTS:
        matches = []
        for path in (dest / "test-evidence" / product["group"]).glob(f'{product["name"]}-UI-*.png'):
            data = path.read_bytes()
            if data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) >= 24:
                size = list(struct.unpack(">II", data[16:24]))
                if size == product["default_size"]:
                    matches.append(path.relative_to(dest).as_posix())
        require(matches, f"Missing fresh compact-size Mac screenshot for {product['name']}")
        ui.append({"name": product["name"], "size": product["default_size"], "screenshots": matches})
    records = [inspect_bundle(dest / "plugins" / (p["name"] + ".vst3"), p, [arch]) for p in PRODUCTS]
    try:
        quality = build_quality_host(source, root, dest, arch, args.jobs, records, snapshot["source_sha256"])
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        write(dest / "native-failure.json", {"passed": False, "architecture": arch, "source": snapshot,
              "completed_groups": suites, "failed_groups": [], "failed_gates": [{"gate": "QualityHost", "error": str(error)}]})
        raise
    require(source_check(source)["source_sha256"] == snapshot["source_sha256"], "Source changed during QualityHost")
    write(dest / "native-build.json", {"passed": True, "architecture": arch, "source": snapshot,
          "native_ctest": suites, "compact_ui_dimensions": ui, "plugins": records, "quality_host": quality,
          "fl_studio_tested": False, "visual_review_required": True})


def merge(args):
    native_arch()
    roots = {arch: Path(getattr(args, arch)).resolve() for arch in ("arm64", "x86_64")}
    reports = {arch: read(root / "native-build.json") for arch, root in roots.items()}
    for arch, report in reports.items():
        require(report["passed"] and report["architecture"] == arch, f"Missing native {arch} build")
        verify_native_quality_gate(report, roots[arch])
    require(reports["arm64"]["source"]["source_sha256"] == reports["x86_64"]["source"]["source_sha256"], "Architecture source mismatch")
    dest = new_destination(args.destination)
    identity = os.environ.get("GILL_APPLICATION_IDENTITY", "").strip()
    if identity:
        require(identity.startswith("Developer ID Application:"), "An Apple Developer ID Application identity is required")
    for product in PRODUCTS:
        name = product["name"] + ".vst3"
        bundles = {arch: root / "plugins" / name for arch, root in roots.items()}
        for arch, bundle in bundles.items():
            record = next(p for p in reports[arch]["plugins"] if p["name"] == product["name"])
            require(bundle_digest(bundle) == record["bundle_sha256"], f"Native bundle changed: {name}/{arch}")
        target = dest / "plugins" / name
        shutil.copytree(bundles["arm64"], target, symlinks=True)
        manifests = {arch: tree_manifest(bundle) for arch, bundle in bundles.items()}
        ignore = lambda relative: relative.startswith("Contents/_CodeSignature/")
        require({n for n in manifests["arm64"] if not ignore(n)} == {n for n in manifests["x86_64"] if not ignore(n)}, f"Bundle layout differs: {name}")
        for relative, record in manifests["arm64"].items():
            if ignore(relative):
                continue
            first, second = bundles["arm64"] / relative, bundles["x86_64"] / relative
            if first.is_file() and not first.is_symlink() and macho(first):
                require(macho(second), f"Intel Mach-O missing: {name}/{relative}")
                run(["lipo", "-create", first, second, "-output", target / relative])
            else:
                require(record == manifests["x86_64"][relative], f"Resource differs across architectures: {name}/{relative}")
        # Our plug-ins contain only one code bundle, no embedded frameworks.
        # Sign contained Mach-O files first if any are added, then the outer VST3.
        for path in sorted(target.rglob("*"), key=lambda p: len(p.parts), reverse=True):
            if path.is_file() and not path.is_symlink() and macho(path):
                command = ["codesign", "--force", "--sign", identity or "-", "--options", "runtime"]
                if identity:
                    command.append("--timestamp")
                run(command + [path])
        command = ["codesign", "--force", "--sign", identity or "-", "--options", "runtime"]
        if identity:
            command.append("--timestamp")
        run(command + [target])
        run(["codesign", "--verify", "--strict", "--deep", target])
    records = [inspect_bundle(dest / "plugins" / (p["name"] + ".vst3"), p, ["arm64", "x86_64"]) for p in PRODUCTS]
    write(dest / "universal-build.json", {"passed": True, "source_sha256": reports["arm64"]["source"]["source_sha256"],
          "signing": "developer-id" if identity else "ad-hoc-local-test-only", "plugins": records,
          "native_builds": reports, "fl_studio_tested": False})


def validate(args):
    arch = native_arch()
    root = Path(args.universal).resolve()
    report = read(root / "universal-build.json")
    require(report["passed"], "Universal build is not complete")
    validator = Path(args.pluginval).resolve()
    require(validator.is_file() and macho(validator), "A real Mac pluginval executable is required")
    require(arch in run(["lipo", "-archs", validator]).split(), "pluginval lacks the native architecture")
    dest = new_destination(args.destination)
    records = []
    for product in PRODUCTS:
        bundle = root / "plugins" / (product["name"] + ".vst3")
        expected = next(p for p in report["plugins"] if p["name"] == product["name"])
        before = bundle_digest(bundle)
        require(before == expected["bundle_sha256"], f"Unvalidated modification: {bundle.name}")
        logdir = dest / product["target"]
        console = run([validator, "--strictness-level", "10", "--random-seed", "20260925",
             "--sample-rates", "44100,48000,88200,96000,192000",
             "--block-sizes", "1,16,64,127,256,512,1024,2048", "--timeout-ms", "120000",
             "--output-dir", logdir, "--output-filename", "validation.txt", "--validate", bundle],
             log=logdir / "console.txt", timeout=2400)
        require(f'{product["name"]} v{product["version"]}' in console, f"Factory version not verified: {bundle.name}")
        require(bundle_digest(bundle) == before, f"Bundle changed during validation: {bundle.name}")
        records.append({"name": product["name"], "bundle_sha256": before, "passed": True, "strictness": 10})
    write(dest / f"validation-{arch}.json", {"passed": True, "native_architecture": arch,
          "universal_report_sha256": sha(root / "universal-build.json"), "plugins": records,
          "validator": "Tracktion pluginval 1.0.4", "validator_binary_sha256": sha(validator)})


def verify_release_gate(universal, validations):
    report = read(Path(universal) / "universal-build.json")
    require(report["passed"] and len(report["plugins"]) == PRODUCT_COUNT, "Universal build is incomplete")
    for arch in ("arm64", "x86_64"):
        native = report.get("native_builds", {}).get(arch, {})
        require(native.get("architecture") == arch, f"Missing native {arch} QualityHost build")
        require(native.get("source", {}).get("source_sha256") == report.get("source_sha256"),
                "QualityHost native evidence belongs to another Universal source build")
        verify_native_quality_gate(native)
        result = read(Path(validations) / arch / f"validation-{arch}.json")
        require(result["passed"] and result["native_architecture"] == arch, f"Native {arch} validation is missing")
        require(result["universal_report_sha256"] == sha(Path(universal) / "universal-build.json"), "Validation report is for another build")
        require(len(result["plugins"]) == PRODUCT_COUNT, "Incomplete validation list")
        for product in PRODUCTS:
            record = next(p for p in result["plugins"] if p["name"] == product["name"])
            bundle = Path(universal) / "plugins" / (product["name"] + ".vst3")
            require(record["passed"] and record["strictness"] == 10 and record["bundle_sha256"] == bundle_digest(bundle), f"Validation no longer matches {bundle.name}")
    return report


def create_source_archive(args):
    source = Path(args.source).resolve()
    source_check(source)
    repo = source.parent
    destination = Path(args.destination).resolve()
    require(not destination.exists(), "Source archive destination already exists")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # The checked-out repository is curated before it is uploaded. Only tracked
    # files and pinned JUCE source go into this corresponding-source archive;
    # job-created signing material and test recordings cannot be swept in.
    selected = {}
    for line in run(["git", "-C", repo, "ls-files", "-z"]).split("\0"):
        if not line:
            continue
        path = repo / line
        if path.is_file():
            selected[line] = path
    juce = source / "dependencies/JUCE"
    for line in run(["git", "-C", juce, "ls-files", "-z"]).split("\0"):
        if line and (juce / line).is_file():
            selected[(juce / line).relative_to(repo).as_posix()] = juce / line
    require(len(selected) > 1000, "Full pinned JUCE source was not collected")
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, path in sorted(selected.items()):
            archive.write(path, name)
    verify_source_archive(destination, source_check(source))
    print("SOURCE ARCHIVE", destination.name, sha(destination))


def verify_source_archive(path, source_report):
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, "Source ZIP CRC check failed")
        names = set(archive.namelist())
        for name, expected in source_report["files"].items():
            if name == "JUCE_PIN":
                continue
            if name == "PRODUCT_CATALOG":
                name = "packaging/macos/products.json"
            if name == "CTEST_MATRIX":
                name = "packaging/macos/ctest-matrix.json"
            archive_name = "work/" + name
            require(archive_name in names, f"Corresponding source is missing: {archive_name}")
            require(hashlib.sha256(archive.read(archive_name)).hexdigest() == expected, f"Source archive mismatch: {archive_name}")
        require("work/dependencies/JUCE/CMakeLists.txt" in names and "work/dependencies/JUCE/LICENSE.md" in names,
                "The source ZIP must include JUCE itself, not only a submodule pointer")


def require_disposable_install_runner(pkg):
    require(sys.platform == "darwin", "Installer execution requires macOS")
    require(os.environ.get("GITHUB_ACTIONS") == "true"
            and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
            and os.environ.get("RUNNER_OS") == "macOS",
            "Installer execution is restricted to a disposable GitHub-hosted macOS runner")
    temporary = os.environ.get("RUNNER_TEMP", "")
    require(temporary and Path(temporary).is_absolute(), "GitHub runner temporary directory is missing")
    require(Path(pkg).resolve().is_relative_to(Path(temporary).resolve()),
            "Installer test package must be inside the GitHub runner temporary directory")


def test_installer(pkg, universal, dest):
    require_disposable_install_runner(pkg)
    installed = Path("/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION")
    require(not installed.exists() or not any(installed.iterdir()),
            "Installer smoke test requires an empty GILLPRODUCTION folder on the disposable runner")
    result = {"passed": False, "environment": "disposable-github-hosted-macos",
              "installer_sha256": sha(pkg), "installation_directory": str(installed), "plugins": []}
    try:
        run(["sudo", "-n", "/usr/sbin/installer", "-pkg", pkg, "-target", "/"],
            log=Path(dest) / "installer-test-console.txt", timeout=600)
        expected_names = {p["name"] + ".vst3" for p in PRODUCTS}
        require({p.name for p in installed.glob("*.vst3")} == expected_names,
                f"Installed bundle list does not contain exactly the expected {PRODUCT_COUNT} plug-ins")
        for product in PRODUCTS:
            name = product["name"] + ".vst3"
            expected = bundle_digest(Path(universal) / "plugins" / name)
            actual = bundle_digest(installed / name)
            require(actual == expected, f"Installer changed or omitted bundle contents: {name}")
            result["plugins"].append({"name": product["name"], "expected_sha256": expected,
                                      "installed_sha256": actual, "passed": True})
        result["passed"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        write(Path(dest) / "installer-test.json", result)
    return result


def package(args):
    native_arch()
    require(args.test_install, "DMG packaging requires --test-install on a disposable GitHub macOS runner")
    universal = Path(args.universal).resolve()
    report = verify_release_gate(universal, args.validations)
    dest = new_destination(args.destination)
    staging = new_destination(args.staging)
    signed = report["signing"] == "developer-id"
    identity = os.environ.get("GILL_INSTALLER_IDENTITY", "").strip()
    profile = os.environ.get("GILL_NOTARY_PROFILE", "").strip()
    require(not args.notarize or signed, "Notarization requires Developer ID Application signed plug-ins")
    if args.notarize:
        require(identity.startswith("Developer ID Installer:") and profile, "Installer identity and keychain notary profile required")
    suffix = "" if args.notarize else "-UNSIGNIERT-TESTVERSION"
    base = f"GILL-PLUGINS-{RELEASE}-MAC{suffix}"
    payload = staging / "payload"
    plugin_dir = payload / "Library/Audio/Plug-Ins/VST3/GILLPRODUCTION"
    shutil.copytree(universal / "plugins", plugin_dir, symlinks=True)
    docs = payload / "Library/Application Support/GILLPRODUCTION"
    docs.mkdir(parents=True)
    for name in ("MAC-INSTALLATION.txt", "products.json"):
        shutil.copy2(HERE / name, docs / name)
    source = Path(args.source).resolve()
    for group in GROUPS:
        project = source / group
        for path in project.rglob("*"):
            relative = path.relative_to(project)
            if path.is_file() and not any(part.startswith("build") for part in relative.parts) and (
                    (len(relative.parts) == 1 and path.name in ("LICENSE", "LICENSE-NOTICE.md", "THIRD-PARTY.md", "THIRD-PARTY-NOTICES.md"))
                    or (relative.parts[0] == "ThirdParty" and path.name.upper().startswith(("LICENSE", "NOTICE", "COPYING")))):
                target = docs / "Lizenzen" / group / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    juce_license = docs / "Lizenzen/JUCE/LICENSE.md"
    juce_license.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "dependencies/JUCE/LICENSE.md", juce_license)
    source_archive = Path(args.source_archive).resolve()
    require(source_archive.is_file() and source_archive.suffix == ".zip", "Corresponding current source ZIP is required for distribution")
    require(source_check(source)["source_sha256"] == report["source_sha256"], "Source archive workspace differs from tested binaries")
    verify_source_archive(source_archive, source_check(source))
    # No privileged scripts, no FL preferences or user projects are modified.
    # The OS Installer owns permissions and same-location bundle replacement.
    components = staging / "components.plist"
    run(["pkgbuild", "--analyze", "--root", payload, components])
    definitions = plistlib.loads(components.read_bytes())
    require(len(definitions) == PRODUCT_COUNT, f"Installer did not discover all {PRODUCT_COUNT} VST3 bundles")
    for item in definitions:
        item["BundleIsRelocatable"] = False
        item["BundleIsVersionChecked"] = False
        item["BundleOverwriteAction"] = "upgrade"
    components.write_bytes(plistlib.dumps(definitions))
    packages = staging / "packages"
    packages.mkdir()
    component = packages / "GILL-VST3.pkg"
    run(["pkgbuild", "--root", payload, "--component-plist", components,
         "--identifier", "com.gillproduction.bundle.vst3", "--version", SUITE_VERSION,
         "--install-location", "/", "--ownership", "recommended", component])
    distribution = ET.Element("installer-gui-script", {"minSpecVersion": "2"})
    ET.SubElement(distribution, "title").text = f"GILLPRODUCTION – {PRODUCT_COUNT} Vocal- und Mixing-Plugins"
    ET.SubElement(distribution, "options", {"customize": "never", "require-scripts": "false", "hostArchitectures": "arm64,x86_64"})
    ET.SubElement(distribution, "domains", {"enable_anywhere": "false", "enable_currentUserHome": "false", "enable_localSystem": "true"})
    allowed = ET.SubElement(distribution, "allowed-os-versions")
    ET.SubElement(allowed, "os-version", {"min": MIN_MACOS})
    ET.SubElement(distribution, "welcome", {"file": "welcome.html", "mime-type": "text/html"})
    ET.SubElement(distribution, "conclusion", {"file": "conclusion.html", "mime-type": "text/html"})
    ET.SubElement(distribution, "choices-outline").append(ET.Element("line", {"choice": "plugins"}))
    choice = ET.SubElement(distribution, "choice", {"id": "plugins", "visible": "false", "title": f"Alle {PRODUCT_COUNT} GILL-Plugins"})
    ET.SubElement(choice, "pkg-ref", {"id": "com.gillproduction.bundle.vst3"})
    ET.SubElement(distribution, "pkg-ref", {"id": "com.gillproduction.bundle.vst3", "version": SUITE_VERSION, "onConclusion": "none"}).text = "GILL-VST3.pkg"
    distribution_path = staging / "Distribution.xml"
    ET.ElementTree(distribution).write(distribution_path, encoding="utf-8", xml_declaration=True)
    command = ["productbuild", "--distribution", distribution_path, "--resources", HERE / "resources", "--package-path", packages]
    if args.notarize:
        command += ["--sign", identity, "--timestamp"]
    pkg = dest / (base + ".pkg")
    run(command + [pkg])
    if args.notarize:
        run(["pkgutil", "--check-signature", pkg], log=dest / "pkg-signature.txt")
        response = json.loads(run(["xcrun", "notarytool", "submit", pkg, "--keychain-profile", profile, "--wait", "--output-format", "json"], timeout=3600))
        write(dest / "pkg-notarization.json", response)
        require(response.get("status") == "Accepted", "Apple did not accept the installer package")
        run(["xcrun", "stapler", "staple", pkg])
        run(["xcrun", "stapler", "validate", pkg])
        run(["spctl", "--assess", "--type", "install", "--verbose", pkg])
    # Exercise the real Apple Installer on the disposable build VM. Only exact
    # copies of all catalogued validated Universal bundles permit a DMG to be produced.
    installation = test_installer(pkg, universal, dest)
    disk = staging / "disk"
    disk.mkdir()
    shutil.copy2(pkg, disk / pkg.name)
    shutil.copy2(HERE / "MAC-INSTALLATION.txt", disk / "ZUERST-LESEN.txt")
    shutil.copy2(source_archive, disk / "GILL-QUELLCODE.zip")
    if not args.notarize:
        (disk / "UNSIGNIERT-TESTVERSION.txt").write_text("Diese echte Mac-Testversion ist nicht mit Apple Developer ID signiert oder notarisiert.\nGatekeeper kann die Installation blockieren. Diese Datei ist keine freigegebene Endkundenversion.\nKeine Systemeinstellungen oder Sicherheitsprüfungen deaktivieren.\n", encoding="utf-8")
    dmg = dest / (base + ".dmg")
    run(["hdiutil", "create", "-volname", "GILL Plugins", "-srcfolder", disk, "-ov", "-format", "UDZO", dmg])
    if args.notarize:
        app_identity = os.environ.get("GILL_APPLICATION_IDENTITY", "").strip()
        require(app_identity.startswith("Developer ID Application:"), "DMG signing identity missing")
        run(["codesign", "--force", "--sign", app_identity, "--timestamp", dmg])
        response = json.loads(run(["xcrun", "notarytool", "submit", dmg, "--keychain-profile", profile, "--wait", "--output-format", "json"], timeout=3600))
        write(dest / "dmg-notarization.json", response)
        require(response.get("status") == "Accepted", "Apple did not accept the disk image")
        run(["xcrun", "stapler", "staple", dmg])
        run(["xcrun", "stapler", "validate", dmg])
        run(["spctl", "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose", dmg])
    run(["hdiutil", "verify", dmg], log=dest / "dmg-verify.txt")
    write(dest / "MAC-RELEASE.json", {"release": RELEASE, "product_count": PRODUCT_COUNT, "architectures": ["arm64", "x86_64"],
          "minimum_macos": MIN_MACOS, "native_validation_passed": True, "notarized": args.notarize,
          "end_user_release_ready": args.notarize, "fl_studio_tested": False,
          "ui_dimensions_verified": True, "visual_review_required": True,
          "installer_execution_passed": installation["passed"], "installed_bundles_verified": len(installation["plugins"]),
          "source_sha256": report["source_sha256"], "source_archive_sha256": sha(source_archive),
          "files": {p.name: sha(p) for p in (pkg, dmg)},
          "install_location": "/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check-source")
    check.add_argument("--source", default=str(HERE.parents[1]))
    source_pack = sub.add_parser("source-archive")
    source_pack.add_argument("--source", required=True)
    source_pack.add_argument("--destination", required=True)
    build_parser = sub.add_parser("build")
    build_parser.add_argument("--source", required=True)
    build_parser.add_argument("--build-root", required=True)
    build_parser.add_argument("--destination", required=True)
    build_parser.add_argument("--jobs", type=int, default=2)
    merge_parser = sub.add_parser("merge")
    merge_parser.add_argument("--arm64", required=True)
    merge_parser.add_argument("--x86_64", required=True)
    merge_parser.add_argument("--destination", required=True)
    validator = sub.add_parser("validate")
    validator.add_argument("--universal", required=True)
    validator.add_argument("--pluginval", required=True)
    validator.add_argument("--destination", required=True)
    pack = sub.add_parser("package")
    pack.add_argument("--universal", required=True)
    pack.add_argument("--validations", required=True)
    pack.add_argument("--source", required=True)
    pack.add_argument("--source-archive", required=True)
    pack.add_argument("--staging", required=True)
    pack.add_argument("--destination", required=True)
    pack.add_argument("--notarize", action="store_true")
    pack.add_argument("--test-install", action="store_true", help="Test the actual PKG installation on a disposable GitHub-hosted Mac; required for DMG output")
    args = parser.parse_args()
    if args.command == "check-source":
        result = source_check(args.source)
        print(json.dumps({k: v for k, v in result.items() if k != "files"}, indent=2))
    else:
        {"build": build, "merge": merge, "validate": validate, "package": package,
         "source-archive": create_source_archive}[args.command](args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError, OSError) as error:
        print(f"STOP: {error}", file=sys.stderr)
        sys.exit(1)
